#!/usr/bin/python
import numpy as np
import sys
import argparse
import os
import matplotlib.pyplot as plt
import matplotlib.mlab as mlab

from scipy.stats import norm
from scipy.stats import entropy
from scipy.special import kl_div
from scipy.special import expit
#from scipy.integrate import quar



#def read_post(frames,alpha=1):
#    c= np.array(frames +alpha, dtype=float)
#    return (c.T/c.sum(axis=1)).T

####################### Function ##############################
## Smoothing with context frames
## Compare prob with epsilon e^-10, if prob < epsilon, prob =epsilon
## KL with sigmoidfunction (expit)

def read_post(post_file,num_phones,epsilon):
    matrix=[]
    with open(post_file,'r') as P:
        P_list=P.readlines()
        for P_line in P_list:
            P_row_list=P_line.split()
            if not len(P_row_list) == num_phones:
               print ('Invalid number of phones: ' + str(len(P_row_list))+ '\n')
               print ('Input number of phones is: ' + str(num_phones) + '\n')
               sys.exit(1)
            else:
               P_row_list=[element_compare_epsilon(float(x),epsilon) for x in P_row_list]
              # print P_row_list
               matrix.append(P_row_list)
    return matrix

def kl_div_smooth(pk,qk):
    pk_max_phone_prob = np.amax(pk)
    #pk_max_phone_id = np.where (pk == pk_max_phone_prob)
    pk_max_phone_id = np.argmax(pk)

    qk_weight_prob = qk[pk_max_phone_id]
    numerator=qk_weight_prob * entropy(pk,qk)
    denominator=qk_weight_prob
    return (numerator,denominator)

#def element_compare_epsilon(post_file,epsilon):
#    output_list=[]
#    for ele in post_file:
#        if ele < epsilon:
#           ele = epsilon
          # print ('ele is : ' + str(ele) + '\n') 
#        output_list.append(ele)
#    print (output_list)
#    return output_list

def element_compare_epsilon(element,epsilon):
    if abs(element) < epsilon:
        element = epsilon
    return element

def KL_compute(X_matrix,Y_matrix,KL_file,num_frames,window):
    KL_vec=[]
    with open(KL_file,'w') as KL:
        for per_frame in xrange (num_frames):
            #print ('Checking zero value ....\n')
            #X_matrix[per_frame]=element_compare_epsilon(X_matrix[per_frame],epsilon)
            #Y_matrix[per_frame]=element_compare_epsilon(Y_matrix[per_frame],epsilon)
            if per_frame < window or per_frame >= num_frames - window:
               KL_per_frame=entropy(X_matrix[per_frame],Y_matrix[per_frame])
            else:
                KL_numerator_sum=0.0
                KL_denominator_sum=0.0
                for window_splice in xrange(per_frame-window,per_frame+window+1): #smoothing with window=6
                    (KL_numerator,KL_denominator)=kl_div_smooth(X_matrix[per_frame],Y_matrix[window_splice])
                    KL_numerator_sum+=KL_numerator
                    KL_denominator_sum+=KL_denominator
                KL_per_frame=KL_numerator_sum/KL_denominator_sum
           # KL_vec.append(expit(KL_per_frame))
            KL_vec.append(KL_per_frame)
            #KL.write(str(expit(KL_per_frame) + ' ')
            KL.write(str(KL_per_frame) + ' ')
    return KL_vec

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('post_X',help='phone post file')
    parser.add_argument('post_Y',help='phone post file')
    parser.add_argument('KL_diver_file',help='output: KL file')
   # parser.add_argument('Name_of_plot',help='plot name')
    parser.add_argument('Num_of_total_phones',help='number of phonemes')
    args=parser.parse_args()

    window=6
    epsilon=10**(-10)

    num_phones=int(args.Num_of_total_phones)
    frames_X=sum(1 for line in open(args.post_X))
    frames_Y=sum(1 for line in open(args.post_Y))


    if frames_X != frames_Y:
       print 'Frames not match! X: %d Y: %d' %(frames_X,frames_Y)
       sys.exit(1)
   
  #  print ('Total number of frames is ' + str(frames_X) + '\n') 

    X_matrix=[]
    Y_matrix=[]
    X_matrix=read_post(args.post_X,num_phones,epsilon)
    Y_matrix=read_post(args.post_Y,num_phones,epsilon)

    KL_vec=[]
    KL_vec=KL_compute(X_matrix,Y_matrix,args.KL_diver_file,frames_X,window)

   # frame_axis=np.arange(frames_X)
   # plt.imshow(KL_vec,origin='lower')
    #plt.show()
   # plt.savefig(args.Name_of_plot)

if __name__=='__main__':
    main()
else:
    raise ImportError('This script cannot be imported')



    
    
    
