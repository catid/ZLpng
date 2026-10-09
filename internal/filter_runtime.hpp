// Filter-only runtime extracted from the validated image experiments.
// No benchmark entry points, image file I/O, ZPNG APIs, or Zstd APIs are used.
#ifndef ZLPNG_INTERNAL_FILTER_RUNTIME_HPP
#define ZLPNG_INTERNAL_FILTER_RUNTIME_HPP
// Reversible prefilters and transform headers used by the ZLP1 container.
// Predictors operate on modulo-2^bits samples, with color lifting before spatial
// prediction (f_) or after it (f2_). Out-of-image neighbors are zero; row and tile
// choices retain context across their boundaries.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zlpng_internal {

using Bytes = std::vector<uint8_t>;
using Samples = std::vector<uint16_t>;
static const size_t HEADER = 24;
static const char* P_NAMES[] = {"raw","left","up","average","paeth","med","gradient","left75","up75","select","left2","up2"};
static const char* C_NAMES[] = {"none","bcif","green","ycocg"};
static const char* L_NAMES[] = {"interleaved","planar","rowplanar"};
static const char* A_NAMES[] = {"fixed","row","tile32","tile64"};
static const char* S_NAMES[] = {"sad","entropy"};
struct Image { uint32_t w=0,h=0,c=0,bits=0; Bytes bytes; };
struct Config {
    int pred=0,color=0,layout=0,split=0,adaptive=0,score=0,set=0,order=0;
    std::string id() const {
        std::string s;
        if(adaptive) s="a_"+std::string(A_NAMES[adaptive])+"_"+S_NAMES[score]+"_"+(set?"full":"basic");
        else s=std::string(order?"f2_":"f_")+P_NAMES[pred];
        return s+"_"+C_NAMES[color]+"_"+L_NAMES[layout]+"_"+(split?"split":"le");
    }
};
static void require(bool b,const std::string& why) { if(!b) throw std::runtime_error(why); }
static uint32_t read32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
static void write32(uint8_t* p,uint32_t v) { for(int i=0;i<4;++i) p[i]=uint8_t(v>>(8*i)); }
static inline int signed_value(int v,int bits) { const int half=1<<(bits-1); return v>=half?v-(1<<bits):v; }
static inline int half_floor(int v) { return v>=0?v/2:-((-v+1)/2); }
static void color_forward_samples(Samples& a,const Image& im,int color) {
    if(!color || im.c<3) return;
    const int mask=(1<<im.bits)-1;
    for(size_t i=0;i<a.size();i+=im.c) {
        int r=a[i],g=a[i+1],b=a[i+2];
        if(color==1) { a[i]=b; a[i+1]=(g-b)&mask; a[i+2]=(g-r)&mask; }
        else if(color==2) { a[i]=g; a[i+1]=(r-g)&mask; a[i+2]=(b-g)&mask; }
        else {
            int co=signed_value((r-b)&mask,im.bits),t=(b+half_floor(co))&mask;
            int cg=signed_value((g-t)&mask,im.bits); a[i]=(t+half_floor(cg))&mask; a[i+1]=co&mask; a[i+2]=cg&mask;
        }
    }
}
static Samples color_forward(const Image& im,int color) {
    Samples a(im.bytes.size()/(im.bits/8));
    if(im.bits==8) std::copy(im.bytes.begin(),im.bytes.end(),a.begin());
    else for(size_t i=0;i<a.size();++i) a[i]=uint16_t(im.bytes[2*i]) | uint16_t(im.bytes[2*i+1])<<8;
    color_forward_samples(a,im,color); return a;
}
static void color_inverse_samples(Samples& a,const Image& im,int color) {
    const int mask=(1<<im.bits)-1;
    if(color && im.c>=3) for(size_t i=0;i<a.size();i+=im.c) {
        int v0=a[i],v1=a[i+1],v2=a[i+2];
        if(color==1) { int g=(v0+v1)&mask; a[i]=(g-v2)&mask; a[i+1]=g; a[i+2]=v0; }
        else if(color==2) { a[i]=(v0+v1)&mask; a[i+1]=v0; a[i+2]=(v0+v2)&mask; }
        else {
            int co=signed_value(v1,im.bits),cg=signed_value(v2,im.bits),t=(v0-half_floor(cg))&mask;
            int b=(t-half_floor(co))&mask; a[i]=(b+co)&mask; a[i+1]=(cg+t)&mask; a[i+2]=b;
        }
    }
}
static Bytes color_inverse(Samples& a,const Image& im,int color) {
    color_inverse_samples(a,im,color);
    Bytes b(a.size()*(im.bits/8));
    if(im.bits==8) for(size_t i=0;i<a.size();++i) b[i]=uint8_t(a[i]);
    else for(size_t i=0;i<a.size();++i) { b[2*i]=uint8_t(a[i]); b[2*i+1]=uint8_t(a[i]>>8); }
    return b;
}
template<int P> static inline int predict(int l,int u,int ul,int ll,int uu) {
    if(P==0) return 0;
    if(P==1) return l;
    if(P==2) return u;
    if(P==3) return (l+u)/2;
    if(P==4) {
        const int p=l+u-ul,dl=std::abs(p-l),du=std::abs(p-u),dul=std::abs(p-ul);
        return dl<=du && dl<=dul?l:du<=dul?u:ul;
    }
    if(P==5) return ul>=std::max(l,u)?std::min(l,u):ul<=std::min(l,u)?std::max(l,u):l+u-ul;
    if(P==6) return l+u-ul;
    if(P==7) return (3*l+u)/4;
    if(P==8) return (l+3*u)/4;
    if(P==9) return std::abs(u-ul)<=std::abs(l-ul)?l:u;
    if(P==10) return 2*l-ll;
    return 2*u-uu;
}
template<int P> static inline int at_predict(const Samples& a,const Image& im,unsigned x,unsigned y,unsigned c) {
    const size_t i=(size_t(y)*im.w+x)*im.c+c,row=size_t(im.w)*im.c;
    const int l=x?a[i-im.c]:0,u=y?a[i-row]:0,ul=x&&y?a[i-row-im.c]:0;
    const int ll=x>1?a[i-2*im.c]:0,uu=y>1?a[i-2*row]:0;
    return predict<P>(l,u,ul,ll,uu);
}
template<int P,bool Decode> static void fixed_filter(const Samples& input,Samples& output,const Image& im) {
    const int mask=(1<<im.bits)-1;
    const Samples& context=Decode?output:input;
    for(unsigned y=0;y<im.h;++y) for(unsigned x=0;x<im.w;++x) for(unsigned c=0;c<im.c;++c) {
        const size_t i=(size_t(y)*im.w+x)*im.c+c;
        const int p=at_predict<P>(context,im,x,y,c);
        output[i]=(int(input[i])+(Decode?p:-p))&mask;
    }
}
template<bool Decode> static void fixed_dispatch(int p,const Samples& input,Samples& output,const Image& im) {
    switch(p) {
#define CASE(P) case P: fixed_filter<P,Decode>(input,output,im); break
        CASE(0); CASE(1); CASE(2); CASE(3); CASE(4); CASE(5); CASE(6); CASE(7); CASE(8); CASE(9); CASE(10); CASE(11);
#undef CASE
    default: throw std::runtime_error("Invalid predictor");
    }
}
static inline int dynamic_predict(int p,const Samples& a,const Image& im,unsigned x,unsigned y,unsigned c) {
    switch(p) {
#define CASE(P) case P: return at_predict<P>(a,im,x,y,c)
        CASE(0); CASE(1); CASE(2); CASE(3); CASE(4); CASE(5); CASE(6); CASE(7); CASE(8); CASE(9); CASE(10); CASE(11);
#undef CASE
    default: throw std::runtime_error("Invalid predictor");
    }
}
static size_t selectors(const Image& im,const Config& q) {
    if(!q.adaptive) return 0;
    const unsigned tw=q.adaptive==1?im.w:q.adaptive==2?32:64,th=q.adaptive==1?1:tw;
    return size_t((im.w+tw-1)/tw)*((im.h+th-1)/th)*im.c;
}
template<bool Decode> static void adaptive_filter(const Samples& input,Samples& output,const Image& im,const Config& q,Bytes& choices) {
    const int basic[]={0,1,2,5},full[]={0,1,2,3,4,5,6,9};
    const int* candidates=q.set?full:basic; const int n=q.set?8:4;
    const unsigned tw=q.adaptive==1?im.w:q.adaptive==2?32:64,th=q.adaptive==1?1:tw;
    const int mask=(1<<im.bits)-1; size_t choice=0;
    const Samples& context=Decode?output:input;
    for(unsigned ty=0;ty<im.h;ty+=th) for(unsigned tx=0;tx<im.w;tx+=tw) for(unsigned c=0;c<im.c;++c) {
        const unsigned xmax=std::min(im.w,tx+tw),ymax=std::min(im.h,ty+th);
        int winner=0;
        if(Decode) { winner=choices.at(choice++); require(winner<12,"Invalid selector"); }
        else {
            double best=std::numeric_limits<double>::infinity();
            for(int k=0;k<n;++k) {
                uint64_t sad=0; std::array<unsigned,256> hist={};
                for(unsigned y=ty;y<ymax;++y) for(unsigned x=tx;x<xmax;++x) {
                    size_t i=(size_t(y)*im.w+x)*im.c+c;
                    int r=(int(input[i])-dynamic_predict(candidates[k],input,im,x,y,c))&mask;
                    if(!q.score) sad+=std::min(r,(mask+1)-r);
                    else { ++hist[r&255]; if(im.bits==16) ++hist[r>>8]; }
                }
                double score=double(sad);
                if(q.score) {
                    const double count=double(xmax-tx)*(ymax-ty)*(im.bits/8);
                    score=count*std::log2(count);
                    for(unsigned h:hist) if(h) score-=double(h)*std::log2(double(h));
                }
                if(score<best) { best=score; winner=candidates[k]; }
            }
            choices[choice++]=uint8_t(winner);
        }
        for(unsigned y=ty;y<ymax;++y) for(unsigned x=tx;x<xmax;++x) {
            size_t i=(size_t(y)*im.w+x)*im.c+c;
            int p=dynamic_predict(winner,context,im,x,y,c);
            output[i]=(int(input[i])+(Decode?p:-p))&mask;
        }
    }
}
static size_t layout_index(const Image& im,int layout,unsigned x,unsigned y,unsigned c) {
    if(layout==1) return (size_t(c)*im.h+y)*im.w+x;
    if(layout==2) return (size_t(y)*im.c+c)*im.w+x;
    return (size_t(y)*im.w+x)*im.c+c;
}
static Bytes filter(const Image& im,const Config& q) {
    Samples colored=color_forward(im,q.order?0:q.color),residual(colored.size()); Bytes choice(selectors(im,q));
    if(q.adaptive) adaptive_filter<false>(colored,residual,im,q,choice); else fixed_dispatch<false>(q.pred,colored,residual,im);
    if(q.order) color_forward_samples(residual,im,q.color);
    const size_t count=colored.size(),offset=choice.size(); Bytes packed(offset+im.bytes.size());
    std::copy(choice.begin(),choice.end(),packed.begin());
    for(unsigned y=0;y<im.h;++y) for(unsigned x=0;x<im.w;++x) for(unsigned c=0;c<im.c;++c) {
        const uint16_t r=residual[(size_t(y)*im.w+x)*im.c+c]; const size_t j=layout_index(im,q.layout,x,y,c);
        if(im.bits==8) packed[offset+j]=uint8_t(r);
        else if(q.split) { packed[offset+j]=uint8_t(r); packed[offset+count+j]=uint8_t(r>>8); }
        else { packed[offset+2*j]=uint8_t(r); packed[offset+2*j+1]=uint8_t(r>>8); }
    }
    return packed;
}
static Bytes unfilter(const Bytes& packed,const Image& im,const Config& q) {
    const size_t count=im.bytes.size()/(im.bits/8),offset=selectors(im,q);
    require(packed.size()==offset+im.bytes.size(),"Filtered payload length mismatch");
    Samples residual(count),colored(count); Bytes choice(packed.begin(),packed.begin()+offset);
    for(unsigned y=0;y<im.h;++y) for(unsigned x=0;x<im.w;++x) for(unsigned c=0;c<im.c;++c) {
        const size_t j=layout_index(im,q.layout,x,y,c); uint16_t r;
        if(im.bits==8) r=packed[offset+j];
        else if(q.split) r=uint16_t(packed[offset+j]) | uint16_t(packed[offset+count+j])<<8;
        else r=uint16_t(packed[offset+2*j]) | uint16_t(packed[offset+2*j+1])<<8;
        residual[(size_t(y)*im.w+x)*im.c+c]=r;
    }
    if(q.order) color_inverse_samples(residual,im,q.color);
    if(q.adaptive) adaptive_filter<true>(residual,colored,im,q,choice); else fixed_dispatch<true>(q.pred,residual,colored,im);
    return color_inverse(colored,im,q.order?0:q.color);
}
#ifdef ZPNG_FUSED8
#include "../experiments/fast_filter.hpp"
#else
static bool fused_supported(const Image&,const Config&) { return false; }
#endif
static Bytes run_filter(const Image& im,const Config& q) {
#ifdef ZPNG_FUSED8
    if(fused_supported(im,q)) return fused_filter(im,q);
#endif
    return filter(im,q);
}
static Bytes run_unfilter(const Bytes& packed,const Image& im,const Config& q) {
#ifdef ZPNG_FUSED8
    if(fused_supported(im,q)) return fused_unfilter(packed,im,q);
#endif
    return unfilter(packed,im,q);
}
static Bytes header(const Image& im,const Config& q) {
    Bytes h(HEADER,0); std::memcpy(h.data(),"ZPF1",4); write32(&h[4],im.w); write32(&h[8],im.h);
    h[12]=uint8_t(im.c); h[13]=uint8_t(im.bits); h[14]=q.pred; h[15]=q.color; h[16]=q.layout; h[17]=q.split;
    h[18]=q.adaptive; h[19]=q.score; h[20]=q.set; h[21]=q.order; return h;
}
static Config read_header(const Bytes& h,const Image& im) {
    require(h.size()>=HEADER && std::memcmp(h.data(),"ZPF1",4)==0,"Invalid experiment header");
    require(read32(&h[4])==im.w && read32(&h[8])==im.h && h[12]==im.c && h[13]==im.bits,"Header dimensions mismatch");
    Config q; q.pred=h[14]; q.color=h[15]; q.layout=h[16]; q.split=h[17]; q.adaptive=h[18]; q.score=h[19]; q.set=h[20]; q.order=h[21];
    require(q.pred<12 && q.color<4 && q.layout<3 && q.split<2 && q.adaptive<4 && q.score<2 && q.set<2 && q.order<2 && !(q.order && q.adaptive),"Invalid experiment config"); return q;
}

#include "../experiments/round2/config.hpp"
#include "../experiments/round2/predictors.hpp"
#include "../experiments/round2/representations.hpp"
#include "../experiments/round2/cache.hpp"
#include "../experiments/round3/config.hpp"
#include "../experiments/round3/common.hpp"
#include "../experiments/round3/predictors.hpp"
#include "../experiments/round3/spatial.hpp"
#include "../experiments/round3/colors.hpp"
#include "../experiments/round3/packing.hpp"
#include "../experiments/round3/blends.hpp"
#include "../experiments/round3/extra_blends.hpp"
static const size_t R2_HEADER=32;
static Bytes r2_run(const Image& im,const R2Config& q,bool decode,const Bytes& input,bool opt) {
    if(q.family<4)return opt?r2p_optimized(im,q,decode,input):r2p_reference(im,q,decode,input);
    if(q.family<9)return opt?r2r_optimized(im,q,decode,input):r2r_reference(im,q,decode,input);
    return opt?r2c_optimized(im,q,decode,input):r2c_reference(im,q,decode,input);
}
static Bytes r2_header(const Image& im,const R2Config& q,size_t payload) {
    Bytes h(R2_HEADER,0);std::memcpy(h.data(),"ZPF2",4);write32(&h[4],im.w);write32(&h[8],im.h);
    h[12]=im.c;h[13]=im.bits;h[14]=q.family;h[15]=q.pred;h[16]=q.color;h[17]=q.layout;h[18]=q.packing;
    write32(&h[20],q.param);write32(&h[24],uint32_t(payload));write32(&h[28],uint32_t(uint64_t(payload)>>32));return h;
}
static R2Config r2_read_header(const Bytes& bytes,const Image& im,size_t& payload) {
    require(bytes.size()>=R2_HEADER&&std::memcmp(bytes.data(),"ZPF2",4)==0,"Invalid R2 header");
    require(read32(&bytes[4])==im.w&&read32(&bytes[8])==im.h&&bytes[12]==im.c&&bytes[13]==im.bits,"R2 image type mismatch");
    R2Config q;q.family=bytes[14];q.pred=bytes[15];q.color=bytes[16];q.layout=bytes[17];q.packing=bytes[18];q.param=int(read32(&bytes[20]));
    require(r2_valid(q,im.bits),"Invalid R2 config");
    const uint64_t count=uint64_t(read32(&bytes[24]))|(uint64_t(read32(&bytes[28]))<<32);
    require(count<=std::numeric_limits<size_t>::max(),"R2 payload length exceeds platform");
    payload=size_t(count);return q;
}

static const size_t R3_HEADER=48;
static Bytes r3_run(const Image& im,const R3Config&q,bool decode,const Bytes& input,bool opt) {
    if(q.family>=31)return r3e_run(im,q,decode,input,opt);
    if(q.family>=26)return r3b_run(im,q,decode,input,opt);
    if(q.family<=6||q.family==14||q.family==25)return r3p_run(im,q,decode,input,opt);
    if(q.family<=12)return r3c_run(im,q,decode,input,opt);
    if(q.family<=20)return r3s_run(im,q,decode,input,opt);
    return r3k_run(im,q,decode,input,opt);
}
static Bytes r3_header(const Image& im,const R3Config&q,size_t payload) {
    R3Writer w;w.bytes(Bytes({'Z','P','F','3'}));w.u32(im.w);w.u32(im.h);
    for(unsigned x:{im.c,im.bits,unsigned(q.family),unsigned(q.pred),unsigned(q.color),unsigned(q.layout),unsigned(q.packing),0u})w.u8(x);
    for(int p:q.p)w.i32(p);w.u64(payload);r3_check(w.data.size()==R3_HEADER,"R3 header size");return std::move(w.data);
}
static R3Config r3_read_header(const Bytes&bytes,const Image&im,size_t&payload) {
    r3_check(bytes.size()>=R3_HEADER&&std::memcmp(bytes.data(),"ZPF3",4)==0,"Invalid R3 header");R3Reader r(bytes);r.pos=4;
    r3_check(r.u32()==im.w,"R3 width mismatch");r3_check(r.u32()==im.h,"R3 height mismatch");r3_check(r.u8()==im.c,"R3 channels mismatch");r3_check(r.u8()==im.bits,"R3 depth mismatch");
    R3Config q;q.family=r.u8();q.pred=r.u8();q.color=r.u8();q.layout=r.u8();q.packing=r.u8();r3_check(r.u8()==0,"R3 reserved byte");for(int&p:q.p)p=r.i32();
    const uint64_t count=r.u64();r3_check(count<=std::numeric_limits<size_t>::max(),"R3 payload length exceeds platform");
    payload=size_t(count);r3_check(r3_valid(q,im.bits),"Invalid R3 config");return q;
}

static Config r3_old_config(const std::string& id, unsigned bits) {
    std::stringstream input(id);
    std::vector<std::string> tokens;
    std::string token;
    while (std::getline(input, token, '_')) tokens.push_back(token);
    auto find = [](const std::string& value, const char* const* names, int count) {
        for (int i = 0; i < count; ++i) if (value == names[i]) return i;
        return -1;
    };
    Config q;
    size_t tail = 0;
    if (tokens.size() == 7 && tokens[0] == "a") {
        q.adaptive = find(tokens[1], A_NAMES, 4);
        q.score = find(tokens[2], S_NAMES, 2);
        q.set = tokens[3] == "full" ? 1 : tokens[3] == "basic" ? 0 : -1;
        require(q.adaptive >= 1 && q.score >= 0 && q.set >= 0, "Invalid adaptive transform");
        tail = 4;
    } else {
        require(tokens.size() == 5 && (tokens[0] == "f" || tokens[0] == "f2"), "Invalid fixed transform");
        q.pred = find(tokens[1], P_NAMES, 12);
        q.order = tokens[0] == "f2";
        require(q.pred >= 0, "Invalid transform predictor");
        tail = 2;
    }
    q.color = find(tokens[tail], C_NAMES, 4);
    q.layout = find(tokens[tail + 1], L_NAMES, 3);
    q.split = tokens[tail + 2] == "le" ? 0 : tokens[tail + 2] == "split" ? 1 : -1;
    require(q.color >= 0 && q.layout >= 0 && q.split >= 0 && (bits == 16 || !q.split) && q.id() == id,
            "Invalid transform identifier");
    return q;
}

#if defined(_MSC_VER)
#define R5_RESTRICT __restrict
#else
#define R5_RESTRICT __restrict__
#endif

template<unsigned Pixel>
static void r5_legacy_plain_row(const uint8_t* R5_RESTRICT src,
                              uint8_t* R5_RESTRICT dst, size_t bytes) {
    std::memcpy(dst, src, Pixel);
    for (size_t i = Pixel; i < bytes; ++i) dst[i] = uint8_t(src[i] - src[i - Pixel]);
}

template<unsigned Pixel>
static void r5_legacy_color_row(const uint8_t* R5_RESTRICT src, unsigned width,
    uint8_t* R5_RESTRICT blue, uint8_t* R5_RESTRICT green_blue,
    uint8_t* R5_RESTRICT green_red, uint8_t* R5_RESTRICT alpha) {
    blue[0] = src[2]; green_blue[0] = uint8_t(src[1] - src[2]);
    green_red[0] = uint8_t(src[1] - src[0]);
    if (Pixel == 4) alpha[0] = src[3];
    for (unsigned x = 1; x < width; ++x) {
        const size_t i = size_t(x) * Pixel;
        const uint8_t r = uint8_t(src[i] - src[i - Pixel]);
        const uint8_t g = uint8_t(src[i + 1] - src[i + 1 - Pixel]);
        const uint8_t b = uint8_t(src[i + 2] - src[i + 2 - Pixel]);
        blue[x] = b; green_blue[x] = uint8_t(g - b); green_red[x] = uint8_t(g - r);
        if (Pixel == 4) alpha[x] = uint8_t(src[i + 3] - src[i + 3 - Pixel]);
    }
}

template<unsigned Pixel>
static void r5_legacy_filter(const ZLPNG_ImageData& source, uint8_t* output) {
    const size_t plane = size_t(source.WidthPixels) * source.HeightPixels;
    for (unsigned y = 0; y < source.HeightPixels; ++y) {
        const uint8_t* src = source.Buffer.Data + size_t(y) * source.StrideBytes;
        const size_t offset = size_t(y) * source.WidthPixels;
        if (Pixel == 3 || Pixel == 4)
            r5_legacy_color_row<Pixel>(src, source.WidthPixels, output + offset,
                output + plane + offset, output + 2 * plane + offset,
                Pixel == 4 ? output + 3 * plane + offset : nullptr);
        else r5_legacy_plain_row<Pixel>(src, output + offset * Pixel,
                                       size_t(source.WidthPixels) * Pixel);
    }
}

#undef R5_RESTRICT

#include "../experiments/round5/fast_runs.hpp"
#include "../experiments/round5/fast_triangular.hpp"
#include "../experiments/round5/fast_common.hpp"
#include "../experiments/round5/fast_blends.hpp"
} // namespace zlpng_internal
#endif
