/usr/local/cuda/bin/nvcc -dc gpu-faster.cu -o gpu-faster.o -I/usr/local/cuda/include -g -Xcompiler -fPIC --verbose --machine 64 -DHAVE_CUDA -ccbin g++ -DKALDI_DOUBLEPRECISION=0 -lcublas -lcublas_device -lcudart -lcudadevrt -gencode arch=compute_60,code=sm_60 -gencode arch=compute_61,code=sm_61 -gencode arch=compute_70,code=sm_70 -I../ -std=c++11 -I../../tools/openfst/include -I../../tools/portaudio/install/include -lrt

/usr/local/cuda/bin/nvcc -dlink -o gpu-faster-link.o gpu-faster.o -I/usr/local/cuda/include -g -Xcompiler -fPIC --verbose --machine 64 -DHAVE_CUDA -ccbin g++ -DKALDI_DOUBLEPRECISION=0 -lcublas -lcublas_device -lcudart -lcudadevrt -gencode arch=compute_60,code=sm_60 -gencode arch=compute_61,code=sm_61 -gencode arch=compute_70,code=sm_70 -I../ -std=c++11 -I../../tools/openfst/include -I../../tools/portaudio/install/include -lrt

g++  -Wl,-rpath=/home/m13514088/kaldi/tools/openfst/lib  -rdynamic -L/usr/local/cuda/lib64 -Wl,-rpath,/usr/local/cuda/lib64 -Wl,-rpath=/home/m13514088/kaldi/src/lib  gpu-faster.o gpu-faster-link.o ../../tools/portaudio/install/lib/libportaudio.a -lrt  ../gpufst/libkaldi-gpufst.so  ../online/libkaldi-online.so  ../decoder/libkaldi-decoder.so  ../lat/libkaldi-lat.so  ../hmm/libkaldi-hmm.so  ../feat/libkaldi-feat.so  ../transform/libkaldi-transform.so  ../gmm/libkaldi-gmm.so  ../tree/libkaldi-tree.so  ../util/libkaldi-util.so  ../matrix/libkaldi-matrix.so  ../base/libkaldi-base.so /home/m13514088/kaldi/tools/openfst/lib/libfst.so /usr/lib/libatlas.so.3 /usr/lib/libf77blas.so.3 /usr/lib/libcblas.so.3 /usr/lib/liblapack_atlas.so.3 -lm -lpthread -ldl -lcublas -lcusparse -lcudart -lcurand -lcudadevrt -lcublas_device -o gpu-faster

