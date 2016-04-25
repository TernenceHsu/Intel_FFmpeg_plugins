/*
 * Intel MediaSDK Quick Sync Video VPP filter
 *
 * copyright (c) 2015 Sven Dueking
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#if 0 
#include <mfx/mfxvideo.h>
#include <mfx/mfxplugin.h>

#include "avfilter.h"
#include "internal.h"

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/avstring.h"
#include "libavutil/error.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/qsv_internal.h"
#endif


#include "internal.h"
#include "vf_vpp.h"
#include <float.h>

/**
 * ToDo :
 *
 * - double check surface pointers for different fourccs
 * - handle empty extbuffers
 * - cropping
 * - use AV_PIX_FMT_QSV to pass surfaces to encoder
 * - deinterlace check settings etc.
 * - allocate number of surfaces depending modules and number of b frames
 */

#define VPP_ZERO_MEMORY(VAR)        { memset(&VAR, 0, sizeof(VAR)); }
#define VPP_ALIGN16(value)          (((value + 15) >> 4) << 4)          // round up to a multiple of 16
#define VPP_ALIGN32(value)          (((value + 31) >> 5) << 5)          // round up to a multiple of 32
#define VPP_CHECK_POINTER(P, ...)   {if (!(P)) {return __VA_ARGS__;}}

// number of video enhancement filters (denoise, procamp, detail, video_analysis, image stab)
//#define ENH_FILTERS_COUNT           5

#if 0
typedef struct {
    const AVClass *class;

    AVFilterContext *ctx;

    mfxSession session;
    QSVSession internal_qs;

    AVRational framerate;                           // target framerate

	QSVFrame *in_work_frames;                       // used for video memory
	QSVFrame *out_work_frames;                      // used for video memory

    mfxFrameSurface1 **in_surface;
    mfxFrameSurface1 **out_surface;

    mfxFrameAllocRequest req[2];                    // [0] - in, [1] - out
    mfxFrameAllocator *pFrameAllocator;	
    mfxFrameAllocResponse* out_response;
	
	int num_surfaces_in;                            // input surfaces
    int num_surfaces_out;                           // output surfaces

    unsigned char * surface_buffers_out;            // output surface buffer

    char *load_plugins;

    mfxVideoParam*      pVppParam;

    /* VPP extension */
    mfxExtBuffer*       pExtBuf[1+ENH_FILTERS_COUNT];
    mfxExtVppAuxData    extVPPAuxData;

    /* Video Enhancement Algorithms */
    mfxExtVPPDeinterlacing  deinterlace_conf;
    mfxExtVPPFrameRateConversion frc_conf;
    mfxExtVPPDenoise denoise_conf;
    mfxExtVPPDetail detail_conf;

    int out_width;
    int out_height;

    int dpic;                   // destination picture structure
                                // -1 = unkown
                                // 0 = interlaced top field first
                                // 1 = progressive
                                // 2 = interlaced bottom field first 

    int deinterlace;            // deinterlace mode : 0=off, 1=bob, 2=advanced
    int denoise;                // Enable Denoise algorithm. Level is the optional value from the interval [0; 100]
    int detail;                 // Enable Detail Enhancement algorithm.
                                // Level is the optional value from the interval [0; 100]
    int async_depth;            // async dept used by encoder
    int max_b_frames;           // maxiumum number of b frames used by encoder

    int cur_out_idx;            // current surface in index

    int frame_number;

    int use_frc;                // use framerate conversion

} VPPContext;
#endif

#define OFFSET(x) offsetof(VPPContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption vpp_options[] = {
    { "deinterlace", "deinterlace mode: 0=off, 1=bob, 2=advanced",             OFFSET(deinterlace),  AV_OPT_TYPE_INT, {.i64=0}, 0, MFX_DEINTERLACING_ADVANCED, .flags = FLAGS },
    { "denoise",     "denoise level [0, 100]",                                 OFFSET(denoise),      AV_OPT_TYPE_INT, {.i64=0}, 0, 100, .flags = FLAGS },
    { "detail",      "detail enhancement level [0, 100]",                      OFFSET(detail),       AV_OPT_TYPE_INT, {.i64=0}, 0, 100, .flags = FLAGS },
    { "w",           "Output video width",                                     OFFSET(out_width),    AV_OPT_TYPE_INT, {.i64=0}, 0, 4096, .flags = FLAGS },
    { "width",       "Output video width",                                     OFFSET(out_width),    AV_OPT_TYPE_INT, {.i64=0}, 0, 4096, .flags = FLAGS },
    { "h",           "Output video height",                                    OFFSET(out_height),   AV_OPT_TYPE_INT, {.i64=0}, 0, 2304, .flags = FLAGS },
    { "height",      "Output video height : ",                                 OFFSET(out_height),   AV_OPT_TYPE_INT, {.i64=0}, 0, 2304, .flags = FLAGS },
    { "dpic",        "dest pic struct: 0=tff, 1=progressive [default], 2=bff", OFFSET(dpic),         AV_OPT_TYPE_INT, {.i64 = 1 }, 0, 2, .flags = FLAGS },
    { "framerate",   "output framerate",                                       OFFSET(framerate),    AV_OPT_TYPE_RATIONAL, { .dbl = 0.0 },0, DBL_MAX, .flags = FLAGS },
    { "async_depth", "Maximum processing parallelism [default = 4]",           OFFSET(async_depth),  AV_OPT_TYPE_INT, { .i64 = ASYNC_DEPTH_DEFAULT }, 0, INT_MAX, .flags = FLAGS },
    { "max_b_frames","Maximum number of b frames  [default = 3]",              OFFSET(max_b_frames), AV_OPT_TYPE_INT, { .i64 = 3 }, 0, INT_MAX, .flags = FLAGS },			
    { NULL }
};

AVFILTER_DEFINE_CLASS(vpp);

static int get_bpp(unsigned int fourcc)
{
    switch (fourcc) {
    case MFX_FOURCC_YUY2:
        return 16;
    case MFX_FOURCC_RGB4:
        return 32;
    default:
        return 12;
    }
    return 12;
}

static int option_id_to_mfx_pic_struct(int id)
{
    switch (id) {
    case 0:
        return MFX_PICSTRUCT_FIELD_TFF;
    case 1:
        return MFX_PICSTRUCT_PROGRESSIVE;
    case 2:
    return MFX_PICSTRUCT_FIELD_BFF;
    default:
    return MFX_PICSTRUCT_UNKNOWN;
        break;
    }
    return MFX_PICSTRUCT_UNKNOWN;
}

static int get_chroma_fourcc(unsigned int fourcc)
{
    switch (fourcc) {
    case MFX_FOURCC_YUY2:
        return MFX_CHROMAFORMAT_YUV422;
    case MFX_FOURCC_RGB4:
        return MFX_CHROMAFORMAT_YUV444;
    default:
        return MFX_CHROMAFORMAT_YUV420;
    }
    return MFX_CHROMAFORMAT_YUV420;
}

static int avframe_id_to_mfx_pic_struct(AVFrame * pic)
{
    if (pic->interlaced_frame == 0)
        return MFX_PICSTRUCT_PROGRESSIVE;

    if (pic->top_field_first == 1)
        return MFX_PICSTRUCT_FIELD_TFF;

    return MFX_PICSTRUCT_FIELD_BFF;
}


static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_YUYV422,
        AV_PIX_FMT_RGB32 ,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int init_vpp_param(AVFilterLink *inlink, AVFrame * pic)
{
    AVFilterContext *ctx = inlink->dst;
    VPPContext *vpp= ctx->priv;

    // input data
    if (inlink->format == AV_PIX_FMT_YUV420P)
        vpp->pVppParam->vpp.In.FourCC = MFX_FOURCC_YV12;
    else if (inlink->format == AV_PIX_FMT_YUYV422)
    vpp->pVppParam->vpp.In.FourCC = MFX_FOURCC_YUY2;
    else if (inlink->format == AV_PIX_FMT_NV12)
        vpp->pVppParam->vpp.In.FourCC = MFX_FOURCC_NV12;
    else
        vpp->pVppParam->vpp.In.FourCC = MFX_FOURCC_RGB4;

    vpp->pVppParam->vpp.In.ChromaFormat = get_chroma_fourcc(vpp->pVppParam->vpp.In.FourCC);
    vpp->pVppParam->vpp.In.CropX = 0;
    vpp->pVppParam->vpp.In.CropY = 0;
    vpp->pVppParam->vpp.In.CropW = inlink->w;
    vpp->pVppParam->vpp.In.CropH = inlink->h;
    vpp->pVppParam->vpp.In.PicStruct = avframe_id_to_mfx_pic_struct(pic);
    vpp->pVppParam->vpp.In.FrameRateExtN = inlink->frame_rate.num;
    vpp->pVppParam->vpp.In.FrameRateExtD = inlink->frame_rate.den;
    vpp->pVppParam->vpp.In.BitDepthLuma   = 8; 
    vpp->pVppParam->vpp.In.BitDepthChroma = 8;

    // width must be a multiple of 16
    // height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture
    vpp->pVppParam->vpp.In.Width = VPP_ALIGN16(inlink->w);
    vpp->pVppParam->vpp.In.Height =
        (MFX_PICSTRUCT_PROGRESSIVE == vpp->pVppParam->vpp.In.PicStruct) ?
        VPP_ALIGN16(inlink->h) :
        VPP_ALIGN32(inlink->h);

    // output data
    vpp->pVppParam->vpp.Out.FourCC = MFX_FOURCC_NV12;
    vpp->pVppParam->vpp.Out.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    vpp->pVppParam->vpp.Out.CropX = 0;
    vpp->pVppParam->vpp.Out.CropY = 0;
    vpp->pVppParam->vpp.Out.CropW = vpp->out_width == 0 ? inlink->w : vpp->out_width;
    vpp->pVppParam->vpp.Out.CropH = vpp->out_height == 0 ? inlink->h : vpp->out_height;
    vpp->pVppParam->vpp.Out.PicStruct = option_id_to_mfx_pic_struct(vpp->dpic);
    vpp->pVppParam->vpp.Out.FrameRateExtN = vpp->framerate.num;
    vpp->pVppParam->vpp.Out.FrameRateExtD = vpp->framerate.den;
    vpp->pVppParam->vpp.Out.BitDepthLuma   = 8;
    vpp->pVppParam->vpp.Out.BitDepthChroma = 8;

    if ((vpp->pVppParam->vpp.In.FrameRateExtN / vpp->pVppParam->vpp.In.FrameRateExtD) != 
        (vpp->pVppParam->vpp.Out.FrameRateExtN / vpp->pVppParam->vpp.Out.FrameRateExtD))
       vpp->use_frc = 1;
    else
        vpp->use_frc = 0;

    // width must be a multiple of 16
    // height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture
    vpp->pVppParam->vpp.Out.Width = VPP_ALIGN16(vpp->pVppParam->vpp.Out.CropW);
    vpp->pVppParam->vpp.Out.Height =
        (MFX_PICSTRUCT_PROGRESSIVE == vpp->pVppParam->vpp.Out.PicStruct) ?
        VPP_ALIGN16(vpp->pVppParam->vpp.Out.CropH) :
        VPP_ALIGN32(vpp->pVppParam->vpp.Out.CropH);

	if(NULL != vpp->pFrameAllocator){
		vpp->pVppParam->IOPattern =  MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;
	}else{
		vpp->pVppParam->IOPattern =  MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
	}

    av_log(ctx, AV_LOG_INFO, "In %dx%d %d fps\t Out %dx%d %d fps\n",
           (vpp->pVppParam->vpp.In.Width),
           (vpp->pVppParam->vpp.In.Height),
           vpp->pVppParam->vpp.In.FrameRateExtN / vpp->pVppParam->vpp.In.FrameRateExtD,
           (vpp->pVppParam->vpp.Out.Width),
           (vpp->pVppParam->vpp.Out.Height),
           vpp->pVppParam->vpp.Out.FrameRateExtN / vpp->pVppParam->vpp.Out.FrameRateExtD);

    if (vpp->use_frc == 1)
        av_log(ctx, AV_LOG_INFO, "Framerate conversion enabled\n");

    return 0;
}

static void vidmem_init_surface(AVFilterLink *inlink)
{
	AVFilterContext *ctx = inlink->dst;
	VPPContext *vpp = ctx->priv;

	av_log( NULL, AV_LOG_INFO, "vpp: vidmem_init_surface:");
	if( NULL != vpp->enc_ctx ){
        vpp->req[1].NumFrameSuggested += vpp->enc_ctx->req.NumFrameSuggested;

	    av_log( NULL, AV_LOG_INFO, " num = %d, enc_ctx.num=%d \n", vpp->req[1].NumFrameSuggested, vpp->enc_ctx->req.NumFrameSuggested );
	}
   // vpp->req[1].NumFrameSuggested = vpp->req[1].NumFrameSuggested * 2;
    vpp->num_surfaces_out = FFMAX(vpp->req[1].NumFrameSuggested, 1);

	if( NULL == vpp->pFrameAllocator) return ;
    vpp->out_response = av_malloc(sizeof(mfxFrameAllocResponse));
    memset( vpp->out_response, 0, sizeof(mfxFrameAllocResponse) );
	

    vpp->pFrameAllocator->Alloc( vpp->pFrameAllocator->pthis, &(vpp->req[1]), vpp->out_response);
   
	vpp->out_surface = av_mallocz(sizeof(mfxFrameSurface1) * vpp->num_surfaces_out);
    VPP_CHECK_POINTER(vpp->out_surface);
    for (int i = 0; i < vpp->num_surfaces_out; i++) {
        vpp->out_surface[i] = av_mallocz(sizeof(mfxFrameSurface1));
        VPP_CHECK_POINTER(vpp->out_surface[i]);
        memset(vpp->out_surface[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(vpp->out_surface[i]->Info), &(vpp->pVppParam->vpp.Out), sizeof(mfxFrameInfo));
        vpp->out_surface[i]->Data.MemId = vpp->out_response->mids[i];
    }
	
}

static void sysmem_init_surface(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    VPPContext *vpp= ctx->priv;

    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int bits_per_pixel = get_bpp(vpp->pVppParam->vpp.In.FourCC);
    unsigned int surface_size = 0;

	av_log( NULL, AV_LOG_INFO, "vpp: sysmem_init_surface\n");
    vpp->num_surfaces_in  = FFMAX(vpp->req[0].NumFrameSuggested, vpp->async_depth + vpp->max_b_frames + 1);
    vpp->num_surfaces_out = FFMAX(vpp->req[1].NumFrameSuggested, 1);

    width = (unsigned int) VPP_ALIGN32(vpp->pVppParam->vpp.In.Width);
    height = (unsigned int) VPP_ALIGN32(vpp->pVppParam->vpp.In.Height);
    surface_size = width * height * bits_per_pixel / 8;
    vpp->in_surface = av_mallocz(sizeof(mfxFrameSurface1) * vpp->num_surfaces_in);
    VPP_CHECK_POINTER(vpp->in_surface);
    for (int i = 0; i < vpp->num_surfaces_in; i++) {
        vpp->in_surface[i] = av_mallocz(sizeof(mfxFrameSurface1));
        VPP_CHECK_POINTER(vpp->in_surface[i]);
        memset(vpp->in_surface[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(vpp->in_surface[i]->Info), &(vpp->pVppParam->vpp.In), sizeof(mfxFrameInfo));
    }


    bits_per_pixel = 12;
    width = (unsigned int) VPP_ALIGN32(vpp->pVppParam->vpp.Out.Width);
    height = (unsigned int) VPP_ALIGN32(vpp->pVppParam->vpp.Out.Height);
    surface_size = width * height * bits_per_pixel / 8;
    vpp->surface_buffers_out = (mfxU8*) av_mallocz(surface_size * vpp->num_surfaces_out);
    VPP_CHECK_POINTER(vpp->surface_buffers_out);

    vpp->out_surface = av_mallocz(sizeof(mfxFrameSurface1) * vpp->num_surfaces_out);
    VPP_CHECK_POINTER(vpp->out_surface);
    for (int i = 0; i < vpp->num_surfaces_out; i++) {
           vpp->out_surface[i] = av_mallocz(sizeof(mfxFrameSurface1));
           VPP_CHECK_POINTER(vpp->out_surface[i]);
            memset(vpp->out_surface[i], 0, sizeof(mfxFrameSurface1));
            memcpy(&(vpp->out_surface[i]->Info), &(vpp->pVppParam->vpp.Out), sizeof(mfxFrameInfo));
            vpp->out_surface[i]->Data.Y = &vpp->surface_buffers_out[surface_size * i];
            vpp->out_surface[i]->Data.U = vpp->out_surface[i]->Data.Y + width * height;
            vpp->out_surface[i]->Data.V = vpp->out_surface[i]->Data.U + 1;
            vpp->out_surface[i]->Data.Pitch = width;
    }
}

 
static int config_vpp(AVFilterLink *inlink, AVFrame * pic)
/* {
    AVFilterContext *ctx = inlink->dst;
    VPPContext *vpp= ctx->priv;

	int ret = 0;

    vpp->pVppParam->vpp.In.PicStruct = avframe_id_to_mfx_pic_struct(pic);
    vpp->pVppParam->IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

	ret = MFXVideoVPP_Init(vpp->session, vpp->pVppParam);
	if (MFX_WRN_PARTIAL_ACCELERATION == ret) {
        av_log(ctx, AV_LOG_WARNING, "VPP will work with partial HW acceleration\n");
    } else if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error initializing the VPP\n");
        return ff_qsv_error(ret);
    }
    
	return ret;
}


static int config_vpp_param(AVFilterLink *inlink)*/
{
    AVFilterContext *ctx = inlink->dst;
    VPPContext *vpp= ctx->priv;

    int ret = 0;

    mfxVideoParam mfxParamsVideo;

	av_log(NULL, AV_LOG_INFO, "vpp configuration and call mfxVideoVPP_Init\n");
    if (!vpp->session) {
        int ret = ff_qsv_init_internal_session((AVCodecContext *)ctx, &vpp->internal_qs,
                                               vpp->load_plugins);

        if (ret < 0)
            return ret;

        vpp->session = vpp->internal_qs.session;
	}

   	av_log(ctx, AV_LOG_INFO, "vpp initializing with session = %p\n", vpp->session);
	VPP_ZERO_MEMORY(mfxParamsVideo);
    vpp->pVppParam = &mfxParamsVideo;

    init_vpp_param(inlink, pic);

    vpp->pVppParam->NumExtParam = 0;
    vpp->frame_number = 0;
    vpp->pVppParam->ExtParam = (mfxExtBuffer**)vpp->pExtBuf;

    if (vpp->deinterlace) {
        memset(&vpp->deinterlace_conf, 0, sizeof(mfxExtVPPDeinterlacing));
        vpp->deinterlace_conf.Header.BufferId = MFX_EXTBUFF_VPP_DEINTERLACING;
        vpp->deinterlace_conf.Header.BufferSz = sizeof(mfxExtVPPDeinterlacing);
        vpp->deinterlace_conf.Mode            = vpp->deinterlace == 1 ? MFX_DEINTERLACING_BOB : MFX_DEINTERLACING_ADVANCED;

        vpp->pVppParam->ExtParam[vpp->pVppParam->NumExtParam++] = (mfxExtBuffer*)&(vpp->deinterlace_conf);
    }

    if (vpp->use_frc) {
        memset(&vpp->frc_conf, 0, sizeof(mfxExtVPPFrameRateConversion));
        vpp->frc_conf.Header.BufferId = MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION;
        vpp->frc_conf.Header.BufferSz = sizeof(mfxExtVPPFrameRateConversion);
        vpp->frc_conf.Algorithm       = MFX_FRCALGM_PRESERVE_TIMESTAMP; // make optional

        vpp->pVppParam->ExtParam[vpp->pVppParam->NumExtParam++] = (mfxExtBuffer*)&(vpp->frc_conf);
    }

    if (vpp->denoise) {
        memset(&vpp->denoise_conf, 0, sizeof(mfxExtVPPDenoise));
        vpp->denoise_conf.Header.BufferId = MFX_EXTBUFF_VPP_DENOISE;
        vpp->denoise_conf.Header.BufferSz = sizeof(mfxExtVPPDenoise);
        vpp->denoise_conf.DenoiseFactor   = vpp->denoise;

        vpp->pVppParam->ExtParam[vpp->pVppParam->NumExtParam++] = (mfxExtBuffer*)&(vpp->denoise_conf);
    }

    if (vpp->detail) {
        memset(&vpp->detail_conf, 0, sizeof(mfxExtVPPDetail));
        vpp->detail_conf.Header.BufferId  = MFX_EXTBUFF_VPP_DETAIL;
        vpp->detail_conf.Header.BufferSz  = sizeof(mfxExtVPPDetail);
        vpp->detail_conf.DetailFactor     = vpp->detail;

        vpp->pVppParam->ExtParam[vpp->pVppParam->NumExtParam++] = (mfxExtBuffer*)&(vpp->detail_conf);
    }

#if 0
    ret = MFXVideoVPP_Query(vpp->session, vpp->pVppParam, vpp->pVppParam);

    if (ret >= MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_INFO, "MFXVideoVPP_Query returned %d \n", ret);
    } else {
        av_log(ctx, AV_LOG_ERROR, "Error %d querying the VPP parameters\n", ret);
        return ff_qsv_error(ret);
    }
#endif

    memset(&vpp->req, 0, sizeof(mfxFrameAllocRequest) * 2);
    ret = MFXVideoVPP_QueryIOSurf(vpp->session, vpp->pVppParam, &vpp->req[0]);
   if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error querying the VPP IO surface\n");
        return ff_qsv_error(ret);
    }

   if( NULL != vpp->pFrameAllocator ){
	   vidmem_init_surface(inlink);
   }else{
	   sysmem_init_surface(inlink);
   }

   ret = MFXVideoVPP_Init(vpp->session, vpp->pVppParam);

   if (MFX_WRN_PARTIAL_ACCELERATION == ret) {
        av_log(ctx, AV_LOG_WARNING, "VPP will work with partial HW acceleration\n");
   } else if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error initializing the VPP\n");
        return ff_qsv_error(ret);
   }

    return 0;
}

static void vidmem_free_surface(AVFilterContext *ctx)
{
    VPPContext *vpp= ctx->priv;

	av_log( NULL, AV_LOG_INFO, "vpp: vidmem_free_surface\n");
    for (unsigned int i = 0; i < vpp->num_surfaces_out; i++)
         av_free(vpp->out_surface[i]);

    av_free(vpp->out_surface);

	if( (NULL != vpp->pFrameAllocator) && (NULL != vpp->out_response) ){
		vpp->pFrameAllocator->Free(vpp->pFrameAllocator->pthis, vpp->out_response);
	}

    vpp->num_surfaces_in  = 0;
    vpp->num_surfaces_out = 0;
}


static void sysmem_free_surface(AVFilterContext *ctx)
{
    VPPContext *vpp= ctx->priv;

	av_log( NULL, AV_LOG_INFO, "vpp: sysmem_free_surface\n");
    for (unsigned int i = 0; i < vpp->num_surfaces_in; i++)
       av_free(vpp->in_surface[i]);

    av_free(vpp->in_surface);

    for (unsigned int i = 0; i < vpp->num_surfaces_out; i++)
         av_free(vpp->out_surface[i]);

    av_free(vpp->out_surface);

    av_free(vpp->surface_buffers_out);

    vpp->num_surfaces_in  = 0;
    vpp->num_surfaces_out = 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterContext *ctx = inlink->dst;
    VPPContext *vpp= ctx->priv;

	/*out wxh should NOT be aligned!!!! keep it origin*/
    outlink->w = vpp->out_width;
    outlink->h = vpp->out_height;

	if(vpp->framerate.den == 0 ||
		vpp->framerate.num == 0)
	{
		vpp->framerate = outlink->src->inputs[0]->frame_rate;
	}

    outlink->frame_rate    = vpp->framerate;
    outlink->time_base     = av_inv_q(vpp->framerate);

    outlink->format = AV_PIX_FMT_NV12;

    return 0;
}

static int get_free_surface_index_in(AVFilterContext *ctx, mfxFrameSurface1 ** surface_pool, int pool_size)
{
    if (surface_pool) {
        for (mfxU16 i = 0; i < pool_size; i++) {
            if (0 == surface_pool[i]->Data.Locked)
                return i;
        }
    }

    av_log(ctx, AV_LOG_ERROR, "Error getting a free surface, pool size is %d\n", pool_size);
    return MFX_ERR_NOT_FOUND;
}

static int get_free_surface_index_out(AVFilterContext *ctx, mfxFrameSurface1 ** surface_pool, int pool_size)
{
    VPPContext *vpp = ctx->priv;

    if (surface_pool) {
        for (mfxU16 i = 0; i < pool_size; i++)
            if (0 == surface_pool[i]->Data.Locked)
               return i;
    }

    av_log(ctx, AV_LOG_ERROR, "Error getting a free surface, pool size is %d\n", pool_size);
    return MFX_ERR_NOT_FOUND;
}

static int output_get_surface( AVFilterLink *inlink, mfxFrameSurface1 **surface )
{
	AVFilterContext *ctx = inlink->dst;
    VPPContext *vpp = ctx->priv;

    int out_idx = 0;

    out_idx = get_free_surface_index_out(ctx, vpp->out_surface, vpp->num_surfaces_out);
 
 	if ( MFX_ERR_NOT_FOUND == out_idx )
        return out_idx;

    
    *surface = vpp->out_surface[out_idx];
    
	return 0;
}

static int sysmem_input_get_surface( AVFilterLink *inlink, AVFrame* picref, mfxFrameSurface1 **surface )
{
	AVFilterContext *ctx = inlink->dst;
    VPPContext *vpp = ctx->priv;

    int in_idx = 0;

	in_idx = get_free_surface_index_in(ctx, vpp->in_surface, vpp->num_surfaces_in);
	

    if ( MFX_ERR_NOT_FOUND == in_idx )
        return in_idx;

    if (inlink->format == AV_PIX_FMT_NV12) {
            vpp->in_surface[in_idx]->Data.Y = picref->data[0];
            vpp->in_surface[in_idx]->Data.VU = picref->data[1];
            vpp->in_surface[in_idx]->Data.Pitch = picref->linesize[0];
    } else if (inlink->format == AV_PIX_FMT_YUV420P) {
            vpp->in_surface[in_idx]->Data.Y = picref->data[0];
            vpp->in_surface[in_idx]->Data.U = picref->data[1];
            vpp->in_surface[in_idx]->Data.V = picref->data[2];
            vpp->in_surface[in_idx]->Data.Pitch = picref->linesize[0];
    } else if (inlink->format == AV_PIX_FMT_YUYV422 ) {
         vpp->in_surface[in_idx]->Data.Y = picref->data[0];
            vpp->in_surface[in_idx]->Data.U = picref->data[0] + 1;
            vpp->in_surface[in_idx]->Data.V = picref->data[0] + 3;
            vpp->in_surface[in_idx]->Data.Pitch = picref->linesize[0];
    } else if (inlink->format == AV_PIX_FMT_RGB32) { 
            vpp->in_surface[in_idx]->Data.B = picref->data[0];
            vpp->in_surface[in_idx]->Data.G = picref->data[0] + 1;
            vpp->in_surface[in_idx]->Data.R = picref->data[0] + 2;
            vpp->in_surface[in_idx]->Data.A = picref->data[0] + 3;
            vpp->in_surface[in_idx]->Data.Pitch = picref->linesize[0];
    }

    *surface = vpp->in_surface[in_idx];

	return 0;
}


static int vidmem_input_get_surface( AVFilterLink *inlink, AVFrame* picref, mfxFrameSurface1 **surface )
{
    if( picref->data[3] != NULL )
    {
        *surface = (mfxFrameSurface1*)picref->data[3];
       // av_log(NULL, AV_LOG_ERROR, "ENCODE: surface MemId=%p Lock=%d\n", (*surface)->Data.MemId, (*surface)->Data.Locked);
        (*surface)->Data.TimeStamp = av_rescale_q(picref->pts, inlink->time_base, (AVRational){1, 90000}); 
        return 0;
    }

    return -1;

}
#if 0
static int vidmem_output_get_surface( VPPContext* vpp, mfxFrameSurface1 **surf)
{
    do {
        *surf = NULL;
	
		QSVFrame *cur_frames = vpp->out_work_frames;
		int i = 0;
		while(NULL != cur_frames){
			if( !(cur_frames->surface->Data.Locked) && !cur_frames->queued ){
               /*   av_log( NULL, AV_LOG_INFO, "selected free surface=%p, NumExtParam=%d, width=%d, height=%d, PitchHigh=%d, PitchLow=%d, Y=%p, UV=%p, corrupted=%d \n",
					                       cur_frames->surface->Data.MemId,
										   cur_frames->surface->Data.NumExtParam,
										   cur_frames->surface->Info.Width, 
										   cur_frames->surface->Info.Height,
										   cur_frames->surface->Data.PitchHigh,
										   cur_frames->surface->Data.PitchLow,
										   cur_frames->surface->Data.Y,
										   cur_frames->surface->Data.UV,
										   cur_frames->surface->Data.Corrupted);*/
                *surf = (cur_frames->surface);
				break;
			}
			cur_frames = cur_frames->next;
			i++;
		}

        if( *surf != NULL ){
			break;
		}
		else{
			av_log( avctx, AV_LOG_ERROR, "waiting until there are free surface" );
			av_usleep(1000);
		}

    } while(1);

    return 0;
}
#endif
static int filter_frame(AVFilterLink *inlink, AVFrame *picref)
{
    AVFilterContext *ctx = inlink->dst;
    VPPContext *vpp = ctx->priv;

    mfxSyncPoint sync = NULL;
    int ret = 0;
    int filter_frame_ret = 0;
    mfxFrameSurface1* pInSurface = NULL;
	mfxFrameSurface1* pOutSurface = NULL;

    AVFilterLink *outlink = inlink->dst->outputs[0];

    AVFrame *out;

    if (vpp->frame_number == 0) 
        config_vpp(inlink, picref);

    do {

		if( NULL != vpp->pFrameAllocator){
			vidmem_input_get_surface( inlink, picref, &pInSurface );
		}else{
			sysmem_input_get_surface( inlink, picref, &pInSurface );
		}

        output_get_surface( inlink, &pOutSurface );

		if( !pInSurface || !pOutSurface ){
			//av_frame_free(&picref);
			av_log(ctx, AV_LOG_ERROR, "no free input or output surface\n");
			return AVERROR(ENOMEM);
		}
        
		//av_log(ctx, AV_LOG_ERROR, "-------width=%d, height=%d, PicStruct=%d, Y=%p, U=%p,V=%p--------\n", 
		//		                  pOutSurface->Info.Width, 
		//		                  pOutSurface->Info.PicStruct, 
		//						  pOutSurface->Info.Height,
		//		                  pOutSurface->Data.Y, 
		//		                  pOutSurface->Data.U, 
		//		                  pOutSurface->Data.V 
		//						  );

        out = ff_get_video_buffer(outlink, vpp->out_width, vpp->out_height); 
        if (!out) {
            av_frame_free(&picref);
            return AVERROR(ENOMEM);
        }
     
        av_frame_copy_props(out, picref);
        
		do {
            
			ret = MFXVideoVPP_RunFrameVPPAsync(vpp->session, pInSurface, pOutSurface, NULL, &sync);

            if (ret == MFX_ERR_MORE_SURFACE) {
                break;
            } else if (ret == MFX_WRN_DEVICE_BUSY) {
                av_usleep(500);
                continue;
            } else if (ret == MFX_ERR_MORE_DATA) {
#if 0
				/*Drop frame here.*/
				continue;
#else
				break;
#endif
            }
            break;
        } while ( 1 );


        if (ret < 0 && ret != MFX_ERR_MORE_SURFACE) {
            if (ret == MFX_ERR_MORE_DATA)
                return 0;
            av_log(ctx, AV_LOG_ERROR, "RunFrameVPPAsync %d\n", ret);
            return ff_qsv_error(ret);
        }

        if (ret == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
            av_log(ctx, AV_LOG_WARNING,
                    "EncodeFrameAsync returned 'incompatible param' code\n");
        }

        MFXVideoCORE_SyncOperation(vpp->session, sync, 60000);

        out->interlaced_frame = vpp->dpic == 1 || vpp->dpic == 3;
        out->top_field_first  = vpp->dpic == 1;
 
		if( NULL != vpp->pFrameAllocator){
    	    out->data[3] = (void*) pOutSurface;
			//av_log(NULL, AV_LOG_ERROR, "------out surface: %p------\n", pOutSurface->Data.MemId);
		}else{
			out->data[0]     = pOutSurface->Data.Y;
			out->data[1]     = pOutSurface->Data.VU;
			out->linesize[0] = pOutSurface->Data.PitchLow;
		}
        out->pts =  av_rescale_q(vpp->frame_number,  av_inv_q(vpp->framerate), outlink->time_base);
        //av_log(NULL, AV_LOG_INFO, " out->pts=%d\n frame number=%d\n inlink->time_base:den=%d, num=%d\n outlink->time_base: den=%d, num=%d\n",
		//		                  out->pts, 
		//						  vpp->frame_number,
	    //						  vpp->framerate.num,
		//						  vpp->framerate.den,
		//						  outlink->time_base.den, 
		//						  outlink->time_base.num );
 
        filter_frame_ret = ff_filter_frame(inlink->dst->outputs[0], out);

        if (filter_frame_ret < 0)
            return ret;

        vpp->frame_number++;

    } while( ret == MFX_ERR_MORE_SURFACE);

    av_frame_free(&picref);

    return 0;
}

static int vpp_process_command(AVFilterContext *ctx, const char *cmd, const char *arg, char *res, int res_len, int flags)
{
    int               ret = MFX_ERR_NONE;
    VPPContext       *vpp = ctx->priv;
    int               index = 0, fd;
    mfxFrameSurface1 *pSurface = NULL;
    AVFrame           frame;
    
    if(cmd == NULL){
        ret = MFX_ERR_NULL_PTR;
        av_log(ctx, AV_LOG_ERROR, "usage: p <filename>\n");
    }else if(!strcmp(cmd, "p")){
        if(NULL == arg){
            ret = MFX_ERR_NULL_PTR;
            av_log(ctx, AV_LOG_ERROR, "usage: p <filename>\n");
        }else{
            index = vpp->cur_out_idx == 0 ? vpp->num_surfaces_out - 1 : vpp->cur_out_idx - 1;
            pSurface = vpp->out_surface[index];
            fd = open(arg, O_WRONLY|O_CREAT);
            write(fd, pSurface->Data.Y, pSurface->Data.PitchLow * pSurface->Info.CropH);
            write(fd, pSurface->Data.UV, pSurface->Data.PitchLow * pSurface->Info.CropH/2);
            close(fd);
        }
    }else{
        ret = MFX_ERR_UNSUPPORTED;
    }
    
    
    return ff_qsv_error(ret);
}

static av_cold int vpp_init(AVFilterContext *ctx)
{
    VPPContext *vpp= ctx->priv;

/* 	av_log(ctx, AV_LOG_ERROR, "vpp initializing with session = %p\n", vpp->session);
    if (!vpp->session) {
        int ret = ff_qsv_init_internal_session((AVCodecContext *)ctx, &vpp->internal_qs,
                                               vpp->load_plugins);

        if (ret < 0)
            return ret;

        vpp->session = vpp->internal_qs.session;
    }
*/
    vpp->frame_number = 0;
    vpp->pFrameAllocator = NULL;
    return 0;
}

static av_cold void vpp_uninit(AVFilterContext *ctx)
{
    VPPContext *vpp= ctx->priv;
    if(NULL != vpp->pFrameAllocator){
		vidmem_free_surface(ctx);
	}else{
		sysmem_free_surface(ctx);
	}
    ff_qsv_close_internal_session(&vpp->internal_qs);
}

static const AVFilterPad vpp_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_input,
        .filter_frame  = filter_frame,

    },
    { NULL }
};

static const AVFilterPad vpp_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_vpp = {
    .name          = "vpp",
    .description   = NULL_IF_CONFIG_SMALL("Quick Sync Video VPP."),
    .priv_size     = sizeof(VPPContext),
    .query_formats = query_formats,
    .init          = vpp_init,
    .uninit        = vpp_uninit,
    .inputs        = vpp_inputs,
    .outputs       = vpp_outputs,
    .priv_class    = &vpp_class,
    .process_command= vpp_process_command,
};
