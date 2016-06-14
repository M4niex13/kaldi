#!/usr/bin/env python

# we're using python 3.x style print but want it to work in python 2.x,
from __future__ import print_function
import os
import argparse
import shlex
import sys
import warnings
import copy
import imp
import ast

nodes = imp.load_source('', 'steps/nnet3/components.py')
nnet3_train_lib = imp.load_source('ntl', 'steps/nnet3/nnet3_train_lib.py')
chain_lib = imp.load_source('ncl', 'steps/nnet3/chain/nnet3_chain_lib.py')

def GetArgs():
    # we add compulsary arguments as named arguments for readability
    parser = argparse.ArgumentParser(description="Writes config files and variables "
                                                 "for TDNNs creation and training",
                                     epilog="See steps/nnet3/tdnn/train.sh for example.")

    # Only one of these arguments can be specified, and one of them has to
    # be compulsarily specified
    feat_group = parser.add_mutually_exclusive_group(required = True)
    feat_group.add_argument("--feat-dim", type=int,
                            help="Raw feature dimension, e.g. 13")
    feat_group.add_argument("--feat-dir", type=str,
                            help="Feature directory, from which we derive the feat-dim")

    # only one of these arguments can be specified
    ivector_group = parser.add_mutually_exclusive_group(required = False)
    ivector_group.add_argument("--ivector-dim", type=int,
                                help="iVector dimension, e.g. 100", default=0)
    ivector_group.add_argument("--ivector-dir", type=str,
                                help="iVector dir, which will be used to derive the ivector-dim  ", default=None)

    num_target_group = parser.add_mutually_exclusive_group(required = True)
    num_target_group.add_argument("--num-targets", type=int,
                                  help="number of network targets (e.g. num-pdf-ids/num-leaves)")
    num_target_group.add_argument("--ali-dir", type=str,
                                  help="alignment directory, from which we derive the num-targets")
    num_target_group.add_argument("--tree-dir", type=str,
                                  help="directory with final.mdl, from which we derive the num-targets")

    parser.add_argument("--num-unfolded-time-steps", type=int,
                        help="number of unfolded time steps.", default=5)
    parser.add_argument("--rnn-dim", type=int,
                        help="dimension of rnn output.")

    # Natural gradient options
    parser.add_argument("--ng-affine-options", type=str,
                        help="options to be supplied to NaturalGradientAffineComponent", default="")

    # General neural network options
    parser.add_argument("--splice-indexes", type=str, required = True,
                        help="Splice indexes at each layer, e.g. '-3,-2,-1,0,1,2,3'")
    parser.add_argument("--add-lda", type=str, action=nnet3_train_lib.StrToBoolAction,
                        help="If \"true\" an LDA matrix computed from the input features "
                        "(spliced according to the first set of splice-indexes) will be used as "
                        "the first Affine layer. This affine layer's parameters are fixed during training. ",
                        default=True, choices = ["false", "true"])

    parser.add_argument("--include-log-softmax", type=str, action=nnet3_train_lib.StrToBoolAction,
                        help="add the final softmax layer ", default=True, choices = ["false", "true"])
    parser.add_argument("--add-final-sigmoid", type=str, action=nnet3_train_lib.StrToBoolAction,
                        help="add a final sigmoid layer as alternate to log-softmax-layer. "
                        "Can only be used if include-log-softmax is false. "
                        "This is useful in cases where you want the output to be "
                        "like probabilities between 0 and 1. Typically the nnet "
                        "is trained with an objective such as quadratic",
                        default=False, choices = ["false", "true"])

    parser.add_argument("--objective-type", type=str,
                        help = "the type of objective; i.e. quadratic or linear",
                        default="linear", choices = ["linear", "quadratic"])
    parser.add_argument("--final-layer-normalize-target", type=float,
                        help="RMS target for final layer (set to <1 if final layer learns too fast",
                        default=1.0)
    parser.add_argument("--relu-dim", type=int,
                        help="dimension of ReLU nonlinearities")

    parser.add_argument("--self-repair-scale", type=float,
                        help="A non-zero value activates the self-repair mechanism in the sigmoid and tanh non-linearities of the LSTM", default=None)


    parser.add_argument("--use-presoftmax-prior-scale", type=str, action=nnet3_train_lib.StrToBoolAction,
                        help="if true, a presoftmax-prior-scale is added",
                        choices=['true', 'false'], default = True)
    parser.add_argument("config_dir",
                        help="Directory to write config files and variables")

    print(' '.join(sys.argv))

    args = parser.parse_args()
    args = CheckArgs(args)

    return args

def CheckArgs(args):
    if not os.path.exists(args.config_dir):
        os.makedirs(args.config_dir)

    ## Check arguments.
    if args.feat_dir is not None:
        args.feat_dim = nnet3_train_lib.GetFeatDim(args.feat_dir)

    if args.ali_dir is not None:
        args.num_targets = nnet3_train_lib.GetNumberOfLeaves(args.ali_dir)
    elif args.tree_dir is not None:
        args.num_targets = chain_lib.GetNumberOfLeaves(args.tree_dir)

    if args.ivector_dir is not None:
        args.ivector_dim = nnet3_train_lib.GetIvectorDim(args.ivector_dir)

    if not args.feat_dim > 0:
        raise Exception("feat-dim has to be postive")

    if not args.num_targets > 0:
        print(args.num_targets)
        raise Exception("num-targets has to be positive")

    if not args.ivector_dim >= 0:
        raise Exception("ivector-dim has to be non-negative")

    if not args.num_unfolded_time_steps > 0:
        raise Exception("num-unfolded-time-steps has to be positive")

    if not args.rnn_dim > 0:
        raise Exception("rnn-dim has to be positive")

    if not args.relu_dim is None:
        args.nonlin_output_dim = args.relu_dim

    if args.add_final_sigmoid and args.include_log_softmax:
        raise Exception("--include-log-softmax and --add-final-sigmoid cannot both be true.")

    return args

def PrintConfig(file_name, config_lines):
    f = open(file_name, 'w')
    f.write("\n".join(config_lines['components'])+"\n")
    f.write("\n#Component nodes\n")
    f.write("\n".join(config_lines['component-nodes']))
    f.close()

def ParseSpliceString(splice_indexes):
    splice_array = []
    left_context = 0
    right_context = 0
    split1 = splice_indexes.split();  # we already checked the string is nonempty.
    if len(split1) < 1:
        raise Exception("invalid splice-indexes argument, too short: "
                 + splice_indexes)
    try:
        for string in split1:
            split2 = string.split(",")
            if len(split2) < 1:
                raise Exception("invalid splice-indexes argument, too-short element: "
                         + splice_indexes)
            int_list = []
            for int_str in split2:
                int_list.append(int(int_str))
            if not int_list == sorted(int_list):
                raise Exception("elements of splice-indexes must be sorted: "
                         + splice_indexes)
            left_context += -int_list[0]
            right_context += int_list[-1]
            splice_array.append(int_list)
    except ValueError as e:
        raise Exception("invalid splice-indexes argument " + splice_indexes + str(e))
    left_context = max(0, left_context)
    right_context = max(0, right_context)

    return {'left_context':left_context,
            'right_context':right_context,
            'splice_indexes':splice_array,
            'num_hidden_layers':len(splice_array)
            }

# The function signature of MakeConfigs is changed frequently as it is intended for local use in this script.
def MakeConfigs(config_dir, splice_indexes_string,
                feat_dim, ivector_dim, num_targets, add_lda,
                num_unfolded_time_steps, rnn_dim,
                nonlin_output_dim,
                use_presoftmax_prior_scale,
                final_layer_normalize_target,
                ng_affine_options,
                include_log_softmax,
                add_final_sigmoid,
                self_repair_scale,
                objective_type):

    parsed_splice_output = ParseSpliceString(splice_indexes_string.strip())

    num_hidden_layers = parsed_splice_output['num_hidden_layers']
    splice_indexes = parsed_splice_output['splice_indexes']

    prior_scale_file = '{0}/presoftmax_prior_scale.vec'.format(config_dir)

    config_lines = {'components':[], 'component-nodes':[]}

    config_files={}
    unfolded_prev_layer_output = nodes.AddUnfoldedInputLayer(config_lines, feat_dim, num_unfolded_time_steps, splice_indexes[0], ivector_dim)

    # Add the init config lines for estimating the preconditioning matrices
    init_config_lines = copy.deepcopy(config_lines)
    init_config_lines['components'].insert(0, '# Config file for initializing neural network prior to')
    init_config_lines['components'].insert(0, '# preconditioning matrix computation')
    nodes.AddOutputLayer(init_config_lines, unfolded_prev_layer_output[-1])
    config_files[config_dir + '/init.config'] = init_config_lines

    if add_lda:
        for i in range(len(unfolded_prev_layer_output)):
            suffix = ("minus{0}".format(len(unfolded_prev_layer_output) - 1 - i) if i != len(unfolded_prev_layer_output) - 1 else "")
            unfolded_prev_layer_output[i] = nodes.AddLdaLayer(config_lines, "L0_t{0}".format(suffix), unfolded_prev_layer_output[i], config_dir + '/lda.mat')

    left_context = num_unfolded_time_steps - 1
    right_context = 0
    # we moved the first splice layer to before the LDA..
    # so the input to the first affine layer is going to [0] index
    splice_indexes[0] = [0]

    # at this point assume only one Unfolded RNN layer
    prev_layer_output = nodes.AddUnfoldedRnnLayer(config_lines, "UnfoldedRnn",
                                                  unfolded_prev_layer_output, rnn_dim,
                                                  ng_affine_options = ng_affine_options,
                                                  self_repair_scale = self_repair_scale)

    # make the intermediate config file for layerwise discriminative training
    nodes.AddFinalLayer(config_lines, prev_layer_output, num_targets,
                        use_presoftmax_prior_scale = use_presoftmax_prior_scale,
                        prior_scale_file = prior_scale_file,
                        include_log_softmax = include_log_softmax)

    config_files['{0}/layer{1}.config'.format(config_dir, 1)] = config_lines
    config_lines = {'components':[], 'component-nodes':[]}

    for i in range(1, num_hidden_layers):
        # make the intermediate config file for layerwise discriminative training

        # prepare the spliced input
        if not (len(splice_indexes[i]) == 1 and splice_indexes[i][0] == 0):
            try:
                zero_index = splice_indexes[i].index(0)
            except ValueError:
                zero_index = None
            appended_descriptors = []
            appended_dimension = 0
            for j in range(len(splice_indexes[i])):
                if j == zero_index:
                    appended_descriptors.append(prev_layer_output['descriptor'])
                    appended_dimension += prev_layer_output['dimension']
                    continue
                appended_descriptors.append('Offset({0}, {1})'.format(prev_layer_output['descriptor'], splice_indexes[i][j]))
                appended_dimension += prev_layer_output['dimension']
            prev_layer_output = {'descriptor' : "Append({0})".format(" , ".join(appended_descriptors)),
                                 'dimension'  : appended_dimension}
        else:
            pass
        prev_layer_output = nodes.AddAffRelNormLayer(config_lines, "L_{0}".format(i),
                                                     prev_layer_output, nonlin_output_dim,
                                                     self_repair_scale = self_repair_scale,
                                                     norm_target_rms = 1.0 if i < num_hidden_layers - 1 else final_layer_normalize_target)

        # make the intermediate config file for layerwise discriminative training
        nodes.AddFinalLayer(config_lines, prev_layer_output, num_targets,
                            use_presoftmax_prior_scale = use_presoftmax_prior_scale,
                            prior_scale_file = prior_scale_file,
                            include_log_softmax = include_log_softmax)

        config_files['{0}/layer{1}.config'.format(config_dir, i+1)] = config_lines
        config_lines = {'components':[], 'component-nodes':[]}

    left_context += int(parsed_splice_output['left_context'])
    right_context += int(parsed_splice_output['right_context'])

    # write the files used by other scripts like steps/nnet3/get_egs.sh
    f = open(config_dir + "/vars", "w")
    print('model_left_context=' + str(left_context), file=f)
    print('model_right_context=' + str(right_context), file=f)
    print('num_hidden_layers=' + str(num_hidden_layers), file=f)
    print('num_targets=' + str(num_targets), file=f)
    print('add_lda=' + ('true' if add_lda else 'false'), file=f)
    print('include_log_softmax=' + ('true' if include_log_softmax else 'false'), file=f)
    print('objective_type=' + objective_type, file=f)
    f.close()

    # printing out the configs
    # init.config used to train lda-mllt train
    for key in config_files.keys():
        PrintConfig(key, config_files[key])

def Main():
    args = GetArgs()

    MakeConfigs(config_dir = args.config_dir,
                splice_indexes_string = args.splice_indexes,
                feat_dim = args.feat_dim, ivector_dim = args.ivector_dim,
                num_targets = args.num_targets,
                add_lda = args.add_lda,
                num_unfolded_time_steps = args.num_unfolded_time_steps,
                rnn_dim = args.rnn_dim,
                nonlin_output_dim = args.nonlin_output_dim,
                use_presoftmax_prior_scale = args.use_presoftmax_prior_scale,
                final_layer_normalize_target = args.final_layer_normalize_target,
                ng_affine_options = args.ng_affine_options,
                include_log_softmax = args.include_log_softmax,
                add_final_sigmoid = args.add_final_sigmoid,
                self_repair_scale = args.self_repair_scale,
                objective_type = args.objective_type)

if __name__ == "__main__":
    Main()

