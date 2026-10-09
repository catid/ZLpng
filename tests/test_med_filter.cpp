#include "../internal/med_filter.hpp"
#include <iostream>
#include <random>

using Bytes=std::vector<uint8_t>;
struct Image{unsigned w,h,c,bits;Bytes bytes;};
static Bytes reference_filter(const Image& image){
    const size_t count=size_t(image.w)*image.h*image.c;
    const unsigned mask=(1u<<image.bits)-1,bytes=image.bits/8;
    std::vector<unsigned> samples(count);
    for(size_t i=0;i<count;++i)samples[i]=image.bytes[i*bytes]+(bytes==2?unsigned(image.bytes[i*bytes+1])<<8:0);
    if(image.bits==8&&image.c>=3)for(size_t i=0;i<count;i+=image.c){
        const unsigned r=samples[i],g=samples[i+1],b=samples[i+2];
        samples[i]=g;samples[i+1]=(r-g)&mask;samples[i+2]=(b-g)&mask;
    }
    Bytes output(count*bytes);
    for(unsigned y=0;y<image.h;++y)for(unsigned x=0;x<image.w;++x)for(unsigned c=0;c<image.c;++c){
        const size_t i=(size_t(y)*image.w+x)*image.c+c,row=size_t(image.w)*image.c;
        const unsigned l=x?samples[i-image.c]:0,u=y?samples[i-row]:0,ul=x&&y?samples[i-row-image.c]:0;
        const unsigned prediction=ul>=std::max(l,u)?std::min(l,u):ul<=std::min(l,u)?std::max(l,u):l+u-ul;
        const unsigned residual=(samples[i]-prediction)&mask;
        const size_t j=(size_t(c)*image.h+y)*image.w+x;
        output[j*bytes]=uint8_t(residual);if(bytes==2)output[j*bytes+1]=uint8_t(residual>>8);
    }
    return output;
}
static void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void parity() {
    std::mt19937 rng(0x4d4544);
    unsigned cases = 0;
    for (unsigned bits : {8u,16u}) for (unsigned channels = 1; channels <= 4; ++channels)
    for (unsigned width : {1u,2u,3u,7u,8u,15u,16u,17u,31u,32u,33u,63u,64u,65u,127u,128u,129u,257u})
    for (unsigned height : {1u,2u,3u,9u}) for (unsigned pattern = 0; pattern < 5; ++pattern) {
        Image image; image.w=width; image.h=height; image.c=channels; image.bits=bits;
        const size_t row=size_t(width)*channels*(bits/8), raw=row*height, stride=row+13;
        image.bytes.resize(raw);
        for (size_t i=0; i<raw; ++i) image.bytes[i]=pattern==0?0:pattern==1?255:pattern==2?(i%2?255:0):pattern==3?uint8_t(i*17):uint8_t(rng());
        Bytes source(stride*height+2,0xa7); uint8_t* input=source.data()+1;
        for (unsigned y=0;y<height;++y) std::memcpy(input+y*stride,image.bytes.data()+y*row,row);
        {
            zlpng_med::Format format{width,height,channels,bits};
            const Bytes expected=reference_filter(image);
            Bytes result(raw+2,0x5a), decoded(stride*height+2,0xc3);
            zlpng_med::encode(input,stride,result.data()+1,format);
            check(std::equal(expected.begin(),expected.end(),result.begin()+1),"Reference payload mismatch");
            check(result.front()==0x5a&&result.back()==0x5a,"Encoded guard overwritten");
            zlpng_med::decode(result.data()+1,decoded.data()+1,stride,format);
            for (unsigned y=0;y<height;++y) {
                check(!std::memcmp(decoded.data()+1+y*stride,image.bytes.data()+y*row,row),"Roundtrip mismatch");
                for (size_t k=row;k<stride;++k) check(decoded[1+y*stride+k]==0xc3,"Stride padding overwritten");
            }
            check(decoded.front()==0xc3&&decoded.back()==0xc3,"Decoded guard overwritten");
            ++cases;
        }
    }
    std::cout<<"MED parity: "<<cases<<" cases passed\n";
}
int main() {
    try { parity(); return 0; }
    catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
