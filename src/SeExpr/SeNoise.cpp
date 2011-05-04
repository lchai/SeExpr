/*
 SEEXPR SOFTWARE
 Copyright 2011 Disney Enterprises, Inc. All rights reserved
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are
 met:
 
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in
 the documentation and/or other materials provided with the
 distribution.
 
 * The names "Disney", "Walt Disney Pictures", "Walt Disney Animation
 Studios" or the names of its contributors may NOT be used to
 endorse or promote products derived from this software without
 specific prior written permission from Walt Disney Pictures.
 
 Disclaimer: THIS SOFTWARE IS PROVIDED BY WALT DISNEY PICTURES AND
 CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE, NONINFRINGEMENT AND TITLE ARE DISCLAIMED.
 IN NO EVENT SHALL WALT DISNEY PICTURES, THE COPYRIGHT HOLDER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND BASED ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
*/

#include <iostream>
#include "SeExprBuiltins.h"
namespace{
#include "SeNoiseTables.h"
}
#include "SeNoise.h"
namespace SeExpr{

//! This is the Quintic interpolant from Perlin's Improved Noise Paper
double s_curve(double t) {
    return t * t * t * (t * ( 6 * t - 15 ) + 10 );
}

//! Does a hash reduce to a character
template<int d> unsigned char hashReduceChar(int index[d])
{
    uint32_t seed=0;
    // blend with seed (constants from Numerical Recipes, attrib. from Knuth)
    for(int k=0;k<d;k++){
        static const uint32_t M = 1664525, C = 1013904223;
        seed=seed*M+index[k]+C;
    }
    // tempering (from Matsumoto)
    seed ^= (seed >> 11);
    seed ^= (seed << 7) & 0x9d2c5680UL;
    seed ^= (seed << 15) & 0xefc60000UL;
    seed ^= (seed >> 18);
    // compute one byte by mixing third and first bytes
    return (((seed&0xff0000) >> 4)+(seed&0xff))&0xff;
}

//! Does a hash reduce to an integer
template<int d> uint32_t hashReduce(uint32_t index[d])
{
    union {
        uint32_t i;
        unsigned char c[4];
    } u1, u2;
    // blend with seed (constants from Numerical Recipes, attrib. from Knuth)
    u1.i=0;
    for(int k=0;k<d;k++){
        static const uint32_t M = 1664525, C = 1013904223;
        u1.i=u1.i*M+index[k]+C;
    }
    // tempering (from Matsumoto)
    u1.i ^= (u1.i >> 11);
    u1.i ^= (u1.i << 7) & 0x9d2c5680U;
    u1.i ^= (u1.i << 15) & 0xefc60000U;
    u1.i ^= (u1.i >> 18);
    // permute bytes (shares perlin noise permutation table)
    u2.c[3] = p[u1.c[0]];
    u2.c[2] = p[u1.c[1]+u2.c[3]];
    u2.c[1] = p[u1.c[2]+u2.c[2]];
    u2.c[0] = p[u1.c[3]+u2.c[1]];
    
    return u2.i;
}

//! Computes cellular noise (non-interpolated piecewise constant cell random values)
template<int d_in,int d_out,class T>
void CellNoise(const T* in,T* out)
{
    uint32_t index[d_in];
    int dim=0;
    for(int k=0;k<d_in;k++) index[k]=uint32_t(floor(in[k]));
    while(1){
        out[dim]=hashReduce<d_in>(index) * (1.0/0xffffffffu);
        if(++dim>=d_out) break;
        for(int k=0;k<d_in;k++) index[k]+=1000;
    }
}

//! Noise with d_in dimensional domain, 1 dimensional abcissa
template<int d,class T,bool periodic>
T noiseHelper(const T* X,const int* period=0)
{
    // find lattice index
    T weights[2][d]; // lower and upper weights
    int index[d];
    for(int k=0;k<d;k++){
        T f=floor(X[k]);
        index[k]=(int)f;
        if(periodic){
            index[k]%=period[k];
            if(index[k]<0) index[k]+=period[k];
        }
        weights[0][k]=X[k]-f;
        weights[1][k]=weights[0][k]-1; // dist to cell with index one above
    }
    // compute function values propagated from zero from each node
    int num=1<<d;
    T vals[num];
    for(int dummy=0;dummy<num;dummy++){
        int latticeIndex[d];
        int offset[d];
        for(int k=0;k<d;k++){
            offset[k]=((dummy&(1<<k))!=0);
            latticeIndex[k]=index[k]+offset[k];
        }
        // hash to get representative gradient vector
        int lookup=hashReduceChar<d>(latticeIndex);
        T val=0;
        for(int k=0;k<d;k++){
            double grad=NOISE_TABLES<d>::g[lookup][k];
            double weight=weights[offset[k]][k];
            val+=grad*weight;
        }
        vals[dummy]=val;
    }
    // compute linear interpolation coefficients
    T alphas[d];
    for(int k=0;k<d;k++) alphas[k]=s_curve(weights[0][k]);
    // perform multilinear interpolation (i.e. linear, bilinear, trilinear, quadralinear)
    for(int newd=d-1;newd>=0;newd--){
        int newnum=1<<newd;
        for(int dummy=0;dummy<newnum;dummy++){
            int index=dummy*(1<<(d-newd));
            int k=(d-newd-1);
            int otherIndex=index+(1<<k);
            //T alpha=s_curve(weights[0][k]);
            T alpha=alphas[k];
            vals[index]=(1-alpha)*vals[index]+alpha*vals[otherIndex];
        }
    }
    // return reduced version
    return vals[0];
}

//! Noise with d_in dimensional domain, d_out dimensional abcissa
template<int d_in,int d_out,class T> void Noise(const T* in,T* out)
{
    T P[d_in];
    for(int i=0;i<d_in;i++) P[i]=in[i];

    int i=0;
    while(1){
        out[i]=noiseHelper<d_in,T,false>(P);
        if(++i>=d_out) break;
        for(int k=0;k<d_out;k++) P[k]+=(T)1000;
    }
}

//! Periodic Noise with d_in dimensional domain, d_out dimensional abcissa
template<int d_in,int d_out,class T> void PNoise(const T* in,const int* period,T* out)
{
    T P[d_in];
    for(int i=0;i<d_in;i++) P[i]=in[i];

    int i=0;
    while(1){
        out[i]=noiseHelper<d_in,T,true>(P,period);
        if(++i>=d_out) break;
        for(int k=0;k<d_out;k++) P[k]+=(T)1000;
    }
}

//! Noise with d_in dimensional domain, d_out dimensional abcissa
//! If turbulence is true then Perlin's turbulence is computed
template<int d_in,int d_out,bool turbulence,class T>
void FBM(const T* in,T* out,
    int octaves,T lacunarity,T gain)
{
    T P[d_in];
    for(int i=0;i<d_in;i++) P[i]=in[i];

    T scale=1;
    for(int k=0;k<d_out;k++) out[k]=0;
    int octave=0;
    while(1){
        T localResult[d_out];
        Noise<d_in,d_out>(P,localResult);
        if(turbulence)
            for(int k=0;k<d_out;k++) out[k]+=fabs(localResult[k])*scale;
        else
            for(int k=0;k<d_out;k++) out[k]+=localResult[k]*scale;
        if(++octave>=octaves)break;
        scale*=gain;
        for(int k=0;k<d_in;k++){
            P[k]*=lacunarity;
            P[k]+=(T)1234;
        }
    }
}

// Explicit instantiations
template void CellNoise<3,1,double>(const double*,double*);
template void CellNoise<3,3,double>(const double*,double*);
template void Noise<1,1,double>(const double*,double*);
template void Noise<2,1,double>(const double*,double*);
template void Noise<3,1,double>(const double*,double*);
template void PNoise<3,1,double>(const double*,const int *,double*);
template void Noise<4,1,double>(const double*,double*);
template void Noise<3,3,double>(const double*,double*);
template void Noise<4,3,double>(const double*,double*);
template void FBM<3,1,false,double>(const double*,double*,int,double,double);
template void FBM<3,1,true,double>(const double*,double*,int,double,double);
template void FBM<3,3,false,double>(const double*,double*,int,double,double);
template void FBM<3,3,true,double>(const double*,double*,int,double,double);
template void FBM<4,1,false,double>(const double*,double*,int,double,double);
template void FBM<4,3,false,double>(const double*,double*,int,double,double);

}

#ifdef MAINTEST
int main(int argc,char* argv[])
{
    typedef double T;
    T sum=0;
    for(int i=0;i<10000000;i++){
        T foo[3]={.3,.3,.3};
        //for(int k=0;k<3;k++) foo[k]=(double)rand()/double(RAND_MAX)*100.;
        sum+=SeExpr::noiseHelper<3,T,false>(foo);

    }
}
#endif
