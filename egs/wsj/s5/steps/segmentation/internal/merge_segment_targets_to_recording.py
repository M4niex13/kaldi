#! /usr/bin/env python

# Copyright 2017  Vimal Manohar
# Apache 2.0

"""
This script merges targets matrices corresponding to
segments into targets matrix for whole recording. The frames that are not
in any of the segments are assigned the default targets vector, specified by
the option --default-targets or [ 0 0 0 ] if unspecified.
"""

import argparse
import logging
import numpy as np
import subprocess
import sys

sys.path.insert(0, 'steps')
import libs.common as common_lib

logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)
handler = logging.StreamHandler()
handler.setLevel(logging.INFO)
formatter = logging.Formatter("%(asctime)s [%(pathname)s:%(lineno)s - "
                              "%(funcName)s - %(levelname)s ] %(message)s")
handler.setFormatter(formatter)
logger.addHandler(handler)


def get_args():
    parser = argparse.ArgumentParser(
        description="""This script merges targets matrices corresponding to
        segments into targets matrix for whole recording.""")

    parser.add_argument("--frame-shift", type=float, default=0.01,
                        help="Frame shift value in seconds")
    parser.add_argument("--default-targets", type=str, default=None,
                        action=common_lib.NullstrToNoneAction,
                        help="Vector of default targets for out-of-segments "
                        "region")
    parser.add_argument("--length-tolerance", type=int, default=4,
                        help="Tolerate length mismatches of this many frames")
    parser.add_argument("--verbose", type=int, default=0, choices=[0,1,2],
                        help="Verbose level")

    parser.add_argument("--reco2num-frames", type=str, required=True,
                        action=common_lib.NullstrToNoneAction,
                        help="""The number of frames per reco
                        is used to determine the num-rows of the output matrix
                        """)
    parser.add_argument("reco2utt", type=str,
                        help="""reco2utt file.
                        The format is <reco> <utt-1> <utt-2> ... <utt-N>""")
    parser.add_argument("segments", type=str,
                        help="Input kaldi segments file")
    parser.add_argument("targets_scp", type=str,
                        help="""SCP of input targets matrices.
                        The matrices are indexed by the utterance-id.""")
    parser.add_argument("out_targets_ark", type=str,
                        help="""Output archive to which the
                        recording-level matrix will be written in text
                        format""")

    args = parser.parse_args()

    if args.frame_shift < 0.0001 or args.frame_shift > 1:
        raise ValueError("--frame-shift should be in [0.0001, 1]; got {0}"
                         "".format(args.frame_shift))

    if args.verbose >= 2:
        logger.setLevel(logging.DEBUG)
        handler.setLevel(logging.DEBUG)

    return args


def run(args):
    reco2utt = {}
    with common_lib.KaldiIo(args.reco2utt) as fh:
        for line in fh:
            parts = line.strip().split()
            if len(parts) < 2:
                raise ValueError("Could not parse line {0}".format(line))
            reco2utt[parts[0]] = parts[1:]

    reco2num_frames = {}
    with common_lib.KaldiIo(args.reco2num_frames) as fh:
        for line in fh:
            parts = line.strip().split()
            if len(parts) != 2:
                raise ValueError("Could not parse line {0}".format(line))
            if parts[0] not in reco2utt:
                continue
            reco2num_frames[parts[0]] = int(parts[1])

    segments = {}
    with common_lib.KaldiIo(args.segments) as fh:
        for line in fh:
            parts = line.strip().split()
            if len(parts) not in [4, 5]:
                raise ValueError("Could not parse line {0}".format(line))
            utt = parts[0]
            reco = parts[1]
            if reco not in reco2utt:
                continue
            start_time = float(parts[2])
            end_time = float(parts[3])
            segments[utt] = [reco, start_time, end_time]

    num_utt_err = 0
    num_utt = 0
    num_reco = 0

    targets = {}
    with common_lib.KaldiIo(args.targets_scp) as fh:
        for line in fh:
            parts = line.strip().split()
            if len(parts) != 2:
                raise ValueError("Could not parse line {0}".format(line))
            utt = parts[0]
            if utt not in segments:
                continue
            targets[utt] = parts[1]

    if args.default_targets is not None:
        default_targets = np.matrix(common_lib.read_matrix_ascii(args.default_targets))
    else:
        default_targets = np.zeros([1, 3])
    assert np.shape(default_targets)[0] == 1 and np.shape(default_targets)[1] == 3

    with common_lib.KaldiIo(args.out_targets_ark, 'w') as fh:
        for reco, utts in reco2utt.iteritems():
            reco_mat = np.repeat(default_targets, reco2num_frames[reco],
                                 axis=0)
            utts.sort(key=lambda x: segments[x][1])   # sort on start time
            for i, utt in enumerate(utts):
                if utt not in segments or utt not in targets:
                    num_utt_err += 1
                    continue
                segment = segments[utt]
                cmd = ("copy-feats --binary=false {mat_fn} -"
                       "".format(mat_fn=targets[utt]))
                p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE)

                try:
                    mat = np.matrix(common_lib.read_matrix_ascii(p.stdout),
                                    dtype='float32')
                except Exception:
                    logger.error("Command '{cmd}' failed".format(cmd=cmd))
                    raise
                finally:
                    [stdout, stderr] = p.communicate()
                    if p.returncode is not None and p.returncode != 0:
                        raise RuntimeError(
                            'Command "{cmd}" failed with status {status}; '
                            'stderr = {stderr}'.format(cmd=cmd, status=-p.returncode,
                                                       stderr=stderr))

                start_frame = int(segment[1] / args.frame_shift + 0.5)
                end_frame = int(segment[2] / args.frame_shift + 0.5)
                num_frames = end_frame - start_frame

                if num_frames <= 0:
                    raise ValueError("Invalid line in segments file {0}"
                                     "".format(segment))

                if abs(mat.shape[0] - num_frames) > args.length_tolerance:
                    logger.warning("For utterance {utt}, mismatch in segment "
                                   "length and targets matrix size; "
                                   "{s_len} vs {t_len}".format(
                                       utt=utt, s_len=num_frames,
                                       t_len=mat.shape[0]))
                    num_utt_err += 1
                    continue

                if end_frame > reco2num_frames[reco]:
                    end_frame = reco2num_frames[reco]
                    num_frames = end_frame - start_frame

                if num_frames < 0:
                    logger.warning("For utterance {utt}, start-frame {start} "
                                   "is outside the recording"
                                   "".format(utt=utt, start=start_frame))
                    num_utt_err += 1
                    continue

                prev_utt_end_frame = (
                    int(segments[utts[i-1]][2] / args.frame_shift + 0.5)
                    if i > 0 else 0)
                if start_frame < prev_utt_end_frame:
                    # Segment overlaps with the previous utterance
                    for n in range(0, prev_utt_end_frame - start_frame):
                        w = float(n) / float(prev_utt_end_frame - start_frame)
                        reco_mat[n + start_frame, :] = (
                            reco_mat[n + start_frame, :] * (1.0 - w)
                            + mat[n, :] * w)

                    num_frames = min(num_frames, mat.shape[0])
                    end_frame = start_frame + num_frames
                    reco_mat[prev_utt_end_frame:end_frame, :] = (
                        mat[(prev_utt_end_frame-start_frame)
                            :(end_frame-start_frame), :])
                else:
                    num_frames = min(num_frames, mat.shape[0])
                    reco_mat[start_frame:(start_frame + num_frames), :] = (
                        mat[0:num_frames, :])
                logger.debug("reco_mat shape = %s, mat shape = %s, "
                             "start_frame = %d, end_frame = %d", reco_mat.shape,
                             mat.shape, start_frame, end_frame)
                num_utt += 1

            if reco_mat.shape[0] > 0:
                common_lib.write_matrix_ascii(fh, reco_mat,
                                              key=reco)
                num_reco += 1

    logger.info("Merged {num_utt} segment targets from {num_reco} recordings; "
                "failed with {num_utt_err} utterances"
                "".format(num_utt=num_utt, num_reco=num_reco,
                          num_utt_err=num_utt_err))

    if num_utt == 0 or num_utt_err > num_utt / 2 or num_reco == 0:
        raise RuntimeError


def main():
    args = get_args()
    try:
        run(args)
    except Exception:
        raise


if __name__ == "__main__":
    main()
