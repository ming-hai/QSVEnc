﻿// -----------------------------------------------------------------------------------------
// QSVEnc by rigaya
// -----------------------------------------------------------------------------------------
// The MIT License
//
// Copyright (c) 2021 rigaya
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// ------------------------------------------------------------------------------------------

#include <set>
#include "qsv_vpp_mfx.h"


static const auto MFX_EXTBUFF_VPP_TO_VPPTYPE = make_array<std::pair<uint32_t, VppType>>(
    std::make_pair(MFX_EXTBUFF_VPP_VIDEO_SIGNAL_INFO,     VppType::MFX_COLORSPACE),
    std::make_pair(0,                                     VppType::MFX_CROP),
    std::make_pair(MFX_EXTBUFF_VPP_ROTATION,              VppType::MFX_ROTATE),
    std::make_pair(MFX_EXTBUFF_VPP_MIRRORING,             VppType::MFX_MIRROR),
    std::make_pair(MFX_EXTBUFF_VPP_DEINTERLACING,         VppType::MFX_DEINTERLACE),
    std::make_pair(MFX_EXTBUFF_VPP_MCTF,                  VppType::MFX_MCTF),
    std::make_pair(MFX_EXTBUFF_VPP_DENOISE,               VppType::MFX_DENOISE),
    std::make_pair(MFX_EXTBUFF_VPP_SCALING,               VppType::MFX_RESIZE),
    std::make_pair(MFX_EXTBUFF_VPP_DETAIL,                VppType::MFX_DETAIL_ENHANCE),
    std::make_pair(MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION, VppType::MFX_FPS_CONV),
    std::make_pair(MFX_EXTBUFF_VPP_IMAGE_STABILIZATION,   VppType::MFX_IMAGE_STABILIZATION)
    );

MAP_PAIR_0_1(vpp, extbuff, uint32_t, rgy, VppType, MFX_EXTBUFF_VPP_TO_VPPTYPE, 0, VppType::VPP_NONE);

QSVVppMfx::QSVVppMfx(std::shared_ptr<CQSVHWDevice> hwdev, QSVAllocator *allocator,
    mfxVersion mfxVer, mfxIMPL impl, MemType memType, int asyncDepth, std::shared_ptr<RGYLog> log) :
    m_mfxSession(),
    m_mfxVer(mfxVer),
    m_hwdev(hwdev),
    m_allocator(allocator),
    m_impl(impl),
    m_memType(memType),
    m_asyncDepth(asyncDepth),
    m_crop(),
    m_mfxVPP(),
    m_mfxVppParams(),
    m_VppDoNotUse(),
    m_VppDoUse(),
    m_ExtDenoise(),
    m_ExtMctf(),
    m_ExtDetail(),
    m_ExtDeinterlacing(),
    m_ExtFrameRateConv(),
    m_ExtRotate(),
    m_ExtVppVSI(),
    m_ExtImageStab(),
    m_ExtMirror(),
    m_ExtScaling(),
    m_VppDoNotUseList(),
    m_VppDoUseList(),
    m_VppExtParams(),
    VppExtMes(),
    m_log(log) {
    InitStructs();
};

void QSVVppMfx::InitStructs() {
    RGY_MEMSET_ZERO(m_mfxVppParams);
    RGY_MEMSET_ZERO(m_VppDoNotUse);
    RGY_MEMSET_ZERO(m_VppDoUse);
    RGY_MEMSET_ZERO(m_ExtDenoise);
    RGY_MEMSET_ZERO(m_ExtMctf);
    RGY_MEMSET_ZERO(m_ExtDetail);
    RGY_MEMSET_ZERO(m_ExtDeinterlacing);
    RGY_MEMSET_ZERO(m_ExtFrameRateConv);
    RGY_MEMSET_ZERO(m_ExtRotate);
    RGY_MEMSET_ZERO(m_ExtVppVSI);
    RGY_MEMSET_ZERO(m_ExtImageStab);
    RGY_MEMSET_ZERO(m_ExtMirror);
    RGY_MEMSET_ZERO(m_ExtScaling);
}

QSVVppMfx::~QSVVppMfx() { clear(); };

void QSVVppMfx::clear() {
    if (m_mfxVPP) {
        m_mfxVPP->Close();
        m_mfxVPP.reset();
    }
    if (m_mfxSession) {
        m_mfxSession.DisjoinSession();
        m_mfxSession.Close();
    }
    m_hwdev.reset();

    m_VppDoNotUseList.clear();
    m_VppDoUseList.clear();
    m_VppExtParams.clear();
    VppExtMes.clear();

    m_log.reset();
}

// ただコピーするためだけのvppを作る
// checkptsで使用する
RGY_ERR QSVVppMfx::SetCopy(const mfxFrameInfo& mfxFrame) {
    if (m_mfxVPP) {
        PrintMes(RGY_LOG_DEBUG, _T("Vpp already initialized.\n"));
        return RGY_ERR_ALREADY_INITIALIZED;
    }
    auto err = InitSession();
    if (err != RGY_ERR_NONE) {
        return err;
    }
    VppExtMes.clear();
    m_mfxVppParams.vpp.In = mfxFrame;
    m_mfxVppParams.vpp.Out = mfxFrame;
    m_mfxVppParams.IOPattern = (m_memType != SYSTEM_MEMORY) ?
        MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY :
        MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    m_VppExtParams.clear();
    m_VppDoUseList.clear();
    m_mfxVppParams.ExtParam = (m_VppExtParams.size()) ? &m_VppExtParams[0] : nullptr;
    m_mfxVppParams.NumExtParam = (mfxU16)m_VppExtParams.size();
    m_mfxVPP = std::make_unique<MFXVideoVPP>(m_mfxSession);
    return RGY_ERR_NONE;
}

RGY_ERR QSVVppMfx::SetParam(
    sVppParams& params,
    const FrameInfo& frameOut,
    const FrameInfo& frameIn,
    const sInputCrop *crop, const rgy_rational<int> infps, const rgy_rational<int> sar, const int blockSize) {
    if (m_mfxVPP) {
        PrintMes(RGY_LOG_DEBUG, _T("Vpp already initialized.\n"));
        return RGY_ERR_ALREADY_INITIALIZED;
    }
    auto err = InitSession();
    if (err != RGY_ERR_NONE) {
        return err;
    }

    err = checkVppParams(params, (frameIn.picstruct & RGY_PICSTRUCT_INTERLACED) != 0);
    if (err != RGY_ERR_NONE) {
        return err;
    }

    VppExtMes.clear();

    auto mfxIn = SetMFXFrameIn(frameIn, crop, infps, sar, blockSize);

    mfxFrameInfo mfxOut;
    if ((err = SetMFXFrameOut(mfxOut, params, frameOut, mfxIn, blockSize)) != RGY_ERR_NONE) {
        return err;
    }

    m_mfxVppParams.vpp.In = mfxIn;
    m_mfxVppParams.vpp.Out = mfxOut;
    m_mfxVppParams.IOPattern = (m_memType != SYSTEM_MEMORY) ?
        MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY :
        MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    if (m_mfxVppParams.vpp.In.FourCC != m_mfxVppParams.vpp.Out.FourCC) {
        vppExtAddMes(strsprintf(_T("ColorFmtConvertion: %s -> %s\n"), ColorFormatToStr(m_mfxVppParams.vpp.In.FourCC), ColorFormatToStr(m_mfxVppParams.vpp.Out.FourCC)));
    }
    if ((err = SetVppExtBuffers(params)) != RGY_ERR_NONE) {
        return err;
    }
    if (GetVppList().size() > 0) {
        m_mfxVPP = std::make_unique<MFXVideoVPP>(m_mfxSession);
    }
    PrintMes(RGY_LOG_DEBUG, _T("Vpp SetParam success.\n"));
    return err;
}

RGY_ERR QSVVppMfx::Init() {
    //ここでの内部エラーは最終的にはmfxライブラリ内部で解決される場合もあり、これをログ上は無視するようにする。
    //具体的にはSandybridgeでd3dメモリでVPPを使用する際、m_pmfxVPP->Init()実行時に
    //"QSVAllocator: Failed CheckRequestType: undeveloped feature"と表示されるが、
    //m_pmfxVPP->Initの戻り値自体はMFX_ERR_NONEであるので、内部で解決されたものと思われる。
    //もちろん、m_pmfxVPP->Init自体がエラーを返した時にはきちんとログに残す。
    const auto log_level = logTemporarilyIgnoreErrorMes();
    auto err = err_to_rgy(m_mfxVPP->Init(&m_mfxVppParams));
    m_log->setLogLevel(log_level);
    if (err == MFX_WRN_PARTIAL_ACCELERATION) {
        PrintMes(RGY_LOG_WARN, _T("partial acceleration on vpp.\n"));
        err = RGY_ERR_NONE;
    }
    if (err != RGY_ERR_NONE) {
        PrintMes(RGY_LOG_ERROR, _T("Failed to initialize vpp: %s.\n"), get_err_mes(err));
        return err;
    }
    PrintMes(RGY_LOG_DEBUG, _T("Vpp initialized.\n"));
    return RGY_ERR_NONE;
}

RGY_ERR QSVVppMfx::Close() {
    if (m_mfxVPP) {
        auto err = err_to_rgy(m_mfxVPP->Close());
        RGY_IGNORE_STS(err, RGY_ERR_NOT_INITIALIZED);
        if (err != RGY_ERR_NONE) {
            PrintMes(RGY_LOG_ERROR, _T("Failed to reset encoder (fail on closing): %s."), get_err_mes(err));
            return err;
        }
        PrintMes(RGY_LOG_DEBUG, _T("Vpp Closed.\n"));
    }
    return RGY_ERR_NONE;
}

RGY_ERR QSVVppMfx::InitSession() {
    // init session, and set memory type
    m_mfxSession.Close();
    auto mfxVer = m_mfxVer;
    auto err = err_to_rgy(m_mfxSession.Init(m_impl, &mfxVer));
    if (err != RGY_ERR_NONE) {
        PrintMes(RGY_LOG_ERROR, _T("Failed to Init session for VPP: %s.\n"), get_err_mes(err));
        return err;
    }

    //使用できる最大のversionをチェック
    m_mfxSession.QueryVersion(&mfxVer);
    if (!check_lib_version(mfxVer, m_mfxVer)) {
        PrintMes(RGY_LOG_ERROR, _T("Session mfxver for VPP does not match version of the base session.\n"));
        return RGY_ERR_UNDEFINED_BEHAVIOR;
    }
    mfxIMPL impl;
    m_mfxSession.QueryIMPL(&impl);
    PrintMes(RGY_LOG_DEBUG, _T("InitSession: mfx lib version: %d.%d, impl 0x%x\n"), m_mfxVer.Major, m_mfxVer.Minor, impl);

    if (impl != MFX_IMPL_SOFTWARE) {
        const auto hdl_t = mfxHandleTypeFromMemType(m_memType);
        mfxHDL hdl = nullptr;
        err = err_to_rgy(m_hwdev->GetHandle(hdl_t, &hdl));
        if (err != RGY_ERR_NONE) {
            PrintMes(RGY_LOG_ERROR, _T("Failed to get HW device handle: %s.\n"), get_err_mes(err));
            return err;
        }
        PrintMes(RGY_LOG_DEBUG, _T("Got HW device handle: %p.\n"), hdl);
        // hwエンコード時のみハンドルを渡す
        err = err_to_rgy(m_mfxSession.SetHandle(hdl_t, hdl));
        if (err != RGY_ERR_NONE) {
            PrintMes(RGY_LOG_ERROR, _T("Failed to set HW device handle to vpp session: %s.\n"), get_err_mes(err));
            return err;
        }
        PrintMes(RGY_LOG_DEBUG, _T("set HW device handle %p to encode session.\n"), hdl);
    }

    if ((err = err_to_rgy(m_mfxSession.SetFrameAllocator(m_allocator))) != RGY_ERR_NONE) {
        PrintMes(RGY_LOG_ERROR, _T("Failed to set frame allocator: %s.\n"), get_err_mes(err));
        return err;
    }

    return RGY_ERR_NONE;
}

RGY_ERR QSVVppMfx::checkVppParams(sVppParams& params, const bool inputInterlaced) {
    const auto availableFeaures = CheckVppFeatures(m_mfxSession, m_mfxVer);
#if ENABLE_FPS_CONVERSION
    if (FPS_CONVERT_NONE != params.nFPSConversion && !(availableFeaures & VPP_FEATURE_FPS_CONVERSION_ADV)) {
        PrintMes(RGY_LOG_WARN, _T("FPS Conversion not supported on this platform, disabled.\n"));
        params.nFPSConversion = FPS_CONVERT_NONE;
    }
#else
    if (params.rotate) {
        if (!(availableFeaures & VPP_FEATURE_ROTATE)) {
            PrintMes(RGY_LOG_ERROR, _T("vpp-rotate is not supported on this platform.\n"));
            return RGY_ERR_UNSUPPORTED;
        }
        if (inputInterlaced) {
            PrintMes(RGY_LOG_ERROR, _T("vpp-rotate is not supported with interlaced output.\n"));
            return RGY_ERR_INVALID_VIDEO_PARAM;
        }
    }
    //現時点ではうまく動いてなさそうなので無効化
    if (params.fpsConversion != FPS_CONVERT_NONE) {
        PrintMes(RGY_LOG_WARN, _T("FPS Conversion not supported on this build, disabled.\n"));
        params.fpsConversion = FPS_CONVERT_NONE;
    }
#endif

    if (params.imageStabilizer && !(availableFeaures & VPP_FEATURE_IMAGE_STABILIZATION)) {
        PrintMes(RGY_LOG_WARN, _T("Image Stabilizer not supported on this platform, disabled.\n"));
        params.imageStabilizer = 0;
    }

    if (inputInterlaced) {
        switch (params.deinterlace) {
        case MFX_DEINTERLACE_IT_MANUAL:
            if (!(availableFeaures & VPP_FEATURE_DEINTERLACE_IT_MANUAL)) {
                PrintMes(RGY_LOG_ERROR, _T("Deinterlace \"it-manual\" is not supported on this platform.\n"));
                return RGY_ERR_INVALID_VIDEO_PARAM;
            }
            break;
        case MFX_DEINTERLACE_AUTO_SINGLE:
        case MFX_DEINTERLACE_AUTO_DOUBLE:
            if (!(availableFeaures & VPP_FEATURE_DEINTERLACE_AUTO)) {
                PrintMes(RGY_LOG_ERROR, _T("Deinterlace \"auto\" is not supported on this platform.\n"));
                return RGY_ERR_INVALID_VIDEO_PARAM;
            }
            break;
        default:
            break;
        }
    }

    if ((params.resizeMode != MFX_SCALING_MODE_DEFAULT || params.resizeInterp != MFX_INTERPOLATION_DEFAULT)
        && !(availableFeaures & VPP_FEATURE_SCALING_QUALITY)) {
        PrintMes(RGY_LOG_WARN, _T("vpp scaling quality is not supported on this platform, disabled.\n"));
        params.resizeMode = MFX_SCALING_MODE_DEFAULT;
        params.resizeInterp = MFX_INTERPOLATION_DEFAULT;
    }

    if (params.mirrorType != MFX_MIRRORING_DISABLED
        && !(availableFeaures & VPP_FEATURE_MIRROR)) {
        PrintMes(RGY_LOG_ERROR, _T("vpp mirroring is not supported on this platform, disabled.\n"));
        return RGY_ERR_UNSUPPORTED;
    }
    return RGY_ERR_NONE;
}

mfxFrameInfo QSVVppMfx::SetMFXFrameIn(const FrameInfo& frameIn, const sInputCrop *crop, const rgy_rational<int> infps, const rgy_rational<int> sar, const int blockSize) {

    auto mfxIn = frameinfo_rgy_to_enc(frameIn, infps, sar, blockSize);

    //QSVデコードを行う場合、CropはVppで行う
    if (crop && cropEnabled(*crop)) {
        mfxIn.CropX = (mfxU16)crop->e.left;
        mfxIn.CropY = (mfxU16)crop->e.up;
        mfxIn.CropW -= (mfxU16)(crop->e.left + crop->e.right);
        mfxIn.CropH -= (mfxU16)(crop->e.bottom + crop->e.up);
        PrintMes(RGY_LOG_DEBUG, _T("SetMFXFrameIn: vpp crop enabled.\n"));
        m_crop = *crop;
    }
    PrintMes(RGY_LOG_DEBUG, _T("SetMFXFrameIn: vpp input frame %dx%d (%d,%d,%d,%d)\n"),
        mfxIn.Width, mfxIn.Height, mfxIn.CropX, mfxIn.CropY, mfxIn.CropW, mfxIn.CropH);
    PrintMes(RGY_LOG_DEBUG, _T("SetMFXFrameIn: vpp input color format %s, chroma %s, bitdepth %d, shift %d, picstruct %s\n"),
        ColorFormatToStr(mfxIn.FourCC), ChromaFormatToStr(mfxIn.ChromaFormat), mfxIn.BitDepthLuma, mfxIn.Shift, MFXPicStructToStr(mfxIn.PicStruct).c_str());
    return mfxIn;
}

RGY_ERR QSVVppMfx::SetMFXFrameOut(mfxFrameInfo& mfxOut, const sVppParams& params, const FrameInfo& frameOut, const mfxFrameInfo& frameIn, const int blockSize) {

    mfxOut = frameIn;

    mfxOut.FourCC = csp_rgy_to_enc(frameOut.csp);
    mfxOut.BitDepthLuma   = (mfxU16)(frameOut.bitdepth > 8 ? frameOut.bitdepth : 0);
    mfxOut.BitDepthChroma = (mfxU16)(frameOut.bitdepth > 8 ? frameOut.bitdepth : 0);
    mfxOut.Shift = (fourccShiftUsed(mfxOut.FourCC) && frameOut.bitdepth < 16) ? 1 : 0;
    mfxOut.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    mfxOut.PicStruct = picstruct_rgy_to_enc(frameOut.picstruct);
    mfxOut.CropX = 0;
    mfxOut.CropY = 0;
    mfxOut.CropW = (mfxU16)frameOut.width;
    mfxOut.CropH = (mfxU16)frameOut.height;
    mfxOut.Width = (mfxU16)ALIGN(frameOut.width, blockSize);
    mfxOut.Height = (mfxU16)ALIGN(frameOut.height, blockSize);

    if ((frameOut.picstruct & RGY_PICSTRUCT_INTERLACED) != 0) {
        INIT_MFX_EXT_BUFFER(m_ExtDeinterlacing, MFX_EXTBUFF_VPP_DEINTERLACING);
        switch (params.deinterlace) {
        case MFX_DEINTERLACE_NORMAL:
        case MFX_DEINTERLACE_AUTO_SINGLE:
            m_ExtDeinterlacing.Mode = (uint16_t)((params.deinterlace == MFX_DEINTERLACE_NORMAL) ? MFX_DEINTERLACING_30FPS_OUT : MFX_DEINTERLACING_AUTO_SINGLE);
            break;
        case MFX_DEINTERLACE_IT:
        case MFX_DEINTERLACE_IT_MANUAL:
            if (params.deinterlace == MFX_DEINTERLACE_IT_MANUAL) {
                m_ExtDeinterlacing.Mode = MFX_DEINTERLACING_FIXED_TELECINE_PATTERN;
                m_ExtDeinterlacing.TelecinePattern = (mfxU16)params.telecinePattern;
            } else {
                m_ExtDeinterlacing.Mode = MFX_DEINTERLACING_24FPS_OUT;
            }
            mfxOut.FrameRateExtN = (mfxOut.FrameRateExtN * 4) / 5;
            break;
        case MFX_DEINTERLACE_BOB:
        case MFX_DEINTERLACE_AUTO_DOUBLE:
            m_ExtDeinterlacing.Mode = (uint16_t)((params.deinterlace == MFX_DEINTERLACE_BOB) ? MFX_DEINTERLACING_BOB : MFX_DEINTERLACING_AUTO_DOUBLE);
            mfxOut.FrameRateExtN *= 2;
            break;
        case MFX_DEINTERLACE_NONE:
            break;
        default:
            PrintMes(RGY_LOG_ERROR, _T("Unknown deinterlace mode.\n"));
            return RGY_ERR_UNSUPPORTED;
        }
        if (params.deinterlace != MFX_DEINTERLACE_NONE) {
#if ENABLE_ADVANCED_DEINTERLACE
            if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_13)) {
                m_VppExtParams.push_back((mfxExtBuffer *)&m_ExtDeinterlacing);
                m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_DEINTERLACING);
            }
#endif
            mfxOut.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
            VppExtMes += _T("Deinterlace (");
            VppExtMes += get_chr_from_value(list_deinterlace, params.deinterlace);
            if (params.deinterlace == MFX_DEINTERLACE_IT_MANUAL) {
                VppExtMes += _T(", ");
                VppExtMes += get_chr_from_value(list_telecine_patterns, params.telecinePattern);
            }
            VppExtMes += _T(")\n");
            PrintMes(RGY_LOG_DEBUG, _T("InitMfxVppParams: vpp deinterlace enabled.\n"));
        }
        if (params.fpsConversion != FPS_CONVERT_NONE) {
            PrintMes(RGY_LOG_ERROR, _T("deinterlace and fps conversion is not supported at the same time.\n"));
            return RGY_ERR_UNSUPPORTED;
        }
    } else {
        switch (params.fpsConversion) {
        case FPS_CONVERT_MUL2:
            mfxOut.FrameRateExtN *= 2;
            break;
        case FPS_CONVERT_MUL2_5:
            mfxOut.FrameRateExtN = mfxOut.FrameRateExtN * 5 / 2;
            break;
        default:
            break;
        }
    }

    PrintMes(RGY_LOG_DEBUG, _T("SetMFXFrameOut: vpp output frame %dx%d (%d,%d,%d,%d)\n"),
        mfxOut.Width, mfxOut.Height, mfxOut.CropX, mfxOut.CropY, mfxOut.CropW, mfxOut.CropH);
    PrintMes(RGY_LOG_DEBUG, _T("SetMFXFrameOut: vpp output color format %s, chroma %s, bitdepth %d, shift %d, picstruct %s\n"),
        ColorFormatToStr(mfxOut.FourCC), ChromaFormatToStr(mfxOut.ChromaFormat), mfxOut.BitDepthLuma, mfxOut.Shift, MFXPicStructToStr(mfxOut.PicStruct).c_str());
    PrintMes(RGY_LOG_DEBUG, _T("SetMFXFrameOut: set all vpp params.\n"));
    return RGY_ERR_NONE;
}

RGY_ERR QSVVppMfx::SetVppExtBuffers(sVppParams& params) {
    m_VppExtParams.clear();
    m_VppDoUseList.clear();
    m_VppDoNotUseList.clear();
    m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_PROCAMP);

    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8)
        && (   MFX_FOURCC_RGB3 == m_mfxVppParams.vpp.In.FourCC
            || MFX_FOURCC_RGB4 == m_mfxVppParams.vpp.In.FourCC
            || params.colorspace.enable)) {

        const bool inputRGB = m_mfxVppParams.vpp.In.FourCC == MFX_FOURCC_RGB3 || m_mfxVppParams.vpp.In.FourCC == MFX_FOURCC_RGB4;

        INIT_MFX_EXT_BUFFER(m_ExtVppVSI, MFX_EXTBUFF_VPP_VIDEO_SIGNAL_INFO);
        m_ExtVppVSI.In.NominalRange    = (mfxU16)((params.colorspace.from.range  == RGY_COLORRANGE_FULL) ? MFX_NOMINALRANGE_0_255   : MFX_NOMINALRANGE_16_235);
        m_ExtVppVSI.In.TransferMatrix  = (mfxU16)((params.colorspace.from.matrix == RGY_MATRIX_ST170_M)  ? MFX_TRANSFERMATRIX_BT601 : MFX_TRANSFERMATRIX_BT709);
        m_ExtVppVSI.Out.NominalRange   = (mfxU16)((params.colorspace.to.range    == RGY_COLORRANGE_FULL) ? MFX_NOMINALRANGE_0_255   : MFX_NOMINALRANGE_16_235);
        m_ExtVppVSI.Out.TransferMatrix = (mfxU16)((params.colorspace.to.matrix   == RGY_MATRIX_ST170_M)  ? MFX_TRANSFERMATRIX_BT601 : MFX_TRANSFERMATRIX_BT709);
        m_VppExtParams.push_back((mfxExtBuffer *)&m_ExtVppVSI);
        m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_VIDEO_SIGNAL_INFO);
        vppExtAddMes(_T("Colorspace\n"));
    } else if(check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_17)) { //なんかMFX_EXTBUFF_VPP_VIDEO_SIGNAL_INFOを設定すると古い環境ではvppの初期化に失敗するらしい。
        m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_VIDEO_SIGNAL_INFO);
    }

    if (params.detail.enable) {
        INIT_MFX_EXT_BUFFER(m_ExtDetail, MFX_EXTBUFF_VPP_DETAIL);
        m_ExtDetail.DetailFactor = (mfxU16)clamp_param_int(params.detail.strength, QSV_VPP_DETAIL_ENHANCE_MIN, QSV_VPP_DETAIL_ENHANCE_MAX, _T("vpp-detail-enhance"));
        m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtDetail);

        vppExtAddMes(strsprintf(_T("Detail Enhancer, strength %d\n"), m_ExtDetail.DetailFactor));
        m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_DETAIL);
    } else {
        m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_DETAIL);
    }

    switch (params.rotate) {
    case MFX_ANGLE_90:
    case MFX_ANGLE_180:
    case MFX_ANGLE_270:
        INIT_MFX_EXT_BUFFER(m_ExtRotate, MFX_EXTBUFF_VPP_ROTATION);
        m_ExtRotate.Angle = (mfxU16)params.rotate;
        m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtRotate);

        vppExtAddMes(strsprintf(_T("rotate %d\n"), params.rotate));
        m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_ROTATION);
        break;
    default:
        break;
    }

    if (params.mirrorType != MFX_MIRRORING_DISABLED) {
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_19)) {
            PrintMes(RGY_LOG_ERROR, _T("--vpp-mirror not supported on this platform, disabled.\n"));
            return RGY_ERR_UNSUPPORTED;
        }
        INIT_MFX_EXT_BUFFER(m_ExtMirror, MFX_EXTBUFF_VPP_MIRRORING);
        m_ExtMirror.Type = (mfxU16)params.mirrorType;
        m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtMirror);

        vppExtAddMes(strsprintf(_T("mirroring %s\n"), get_chr_from_value(list_vpp_mirroring, params.mirrorType)));
        m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_MIRRORING);
    }

    if (    m_mfxVppParams.vpp.Out.CropW != m_mfxVppParams.vpp.In.CropW
         || m_mfxVppParams.vpp.Out.CropH != m_mfxVppParams.vpp.In.CropH) {
        auto str = strsprintf(_T("Resize %dx%d -> %dx%d"), m_mfxVppParams.vpp.In.CropW, m_mfxVppParams.vpp.In.CropH, m_mfxVppParams.vpp.Out.CropW, m_mfxVppParams.vpp.Out.CropH);
        if ((check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_19) && params.resizeMode != MFX_SCALING_MODE_DEFAULT)
            || (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_33) && params.resizeInterp != MFX_INTERPOLATION_DEFAULT)) {
            INIT_MFX_EXT_BUFFER(m_ExtScaling, MFX_EXTBUFF_VPP_SCALING);
            if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_33)) {
                m_ExtScaling.ScalingMode = (mfxU16)params.resizeInterp; // API 1.33
                str += strsprintf(_T(", %s"), get_chr_from_value(list_vpp_resize, resize_algo_enc_to_rgy(params.resizeInterp)));
            }
            m_ExtScaling.ScalingMode = (mfxU16)params.resizeMode; // API 1.19
            str += strsprintf(_T(", %s"), get_chr_from_value(list_vpp_resize_mode, resize_mode_enc_to_rgy(params.resizeMode)));
            m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtScaling);
            m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_SCALING);
        }
        vppExtAddMes(str + _T("\n"));
    }

    if (params.fpsConversion != FPS_CONVERT_NONE) {
        INIT_MFX_EXT_BUFFER(m_ExtFrameRateConv, MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION);
        m_ExtFrameRateConv.Algorithm = MFX_FRCALGM_FRAME_INTERPOLATION;
        m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtFrameRateConv);

        vppExtAddMes(_T("fps conversion with interpolation\n"));
        m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION);
    }

    if (params.denoise.enable) {
        INIT_MFX_EXT_BUFFER(m_ExtDenoise, MFX_EXTBUFF_VPP_DENOISE);
        m_ExtDenoise.DenoiseFactor = (mfxU16)clamp_param_int(params.denoise.strength, QSV_VPP_DENOISE_MIN, QSV_VPP_DENOISE_MAX, _T("vpp-denoise"));
        m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtDenoise);

        vppExtAddMes(strsprintf(_T("Denoise, strength %d\n"), m_ExtDenoise.DenoiseFactor));
        m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_DENOISE);
    } else {
        m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_DENOISE);
    }

    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_26)) {
        if (params.mctf.enable) {
            INIT_MFX_EXT_BUFFER(m_ExtMctf, MFX_EXTBUFF_VPP_MCTF);
            m_ExtMctf.FilterStrength = (mfxU16)clamp_param_int(params.mctf.strength, 0, QSV_VPP_MCTF_MAX, _T("vpp-mctf"));
            m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtMctf);

            if (m_ExtMctf.FilterStrength == 0) {
                vppExtAddMes(_T("mctf, strength auto\n"));
            } else {
                vppExtAddMes(strsprintf(_T("mctf, strength %d\n"), m_ExtMctf.FilterStrength));
            }
            m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_MCTF);
        } else {
            m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_MCTF);
        }
    } else {
        if (params.mctf.enable) {
            PrintMes(RGY_LOG_WARN, _T("--vpp-mctf not supported on this platform, disabled.\n"));
            params.mctf.enable = false;
        }
    }

    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_6)) {
        if (params.imageStabilizer) {
            if (CheckParamList(params.imageStabilizer, list_vpp_image_stabilizer, "vpp-image-stab") != RGY_ERR_NONE) {
                PrintMes(RGY_LOG_ERROR, _T("invalid stabilizer mode selected.\n"));
                return RGY_ERR_INVALID_PARAM;
            }
            INIT_MFX_EXT_BUFFER(m_ExtImageStab, MFX_EXTBUFF_VPP_IMAGE_STABILIZATION);
            m_ExtImageStab.Mode = (mfxU16)params.imageStabilizer;
            m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtImageStab);

            vppExtAddMes(strsprintf(_T("Stabilizer, mode %s\n"), get_vpp_image_stab_mode_str(m_ExtImageStab.Mode)));
            m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_IMAGE_STABILIZATION);
        }
    } else {
        if (params.imageStabilizer) {
            PrintMes(RGY_LOG_WARN, _T("--vpp-image-stab not supported on this platform, disabled.\n"));
            params.imageStabilizer = 0;
        }
    }

    m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_SCENE_ANALYSIS);

    if (   check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_3)
        && m_mfxVppParams.vpp.In.PicStruct != m_mfxVppParams.vpp.Out.PicStruct
        && m_mfxVppParams.vpp.Out.PicStruct == MFX_PICSTRUCT_PROGRESSIVE) {
            switch (params.deinterlace) {
            case MFX_DEINTERLACE_IT:
            case MFX_DEINTERLACE_IT_MANUAL:
            case MFX_DEINTERLACE_BOB:
            case MFX_DEINTERLACE_AUTO_DOUBLE:
                INIT_MFX_EXT_BUFFER(m_ExtFrameRateConv, MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION);
                m_ExtFrameRateConv.Algorithm = MFX_FRCALGM_DISTRIBUTED_TIMESTAMP;

                m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION);
                break;
            default:
                break;
            }
    }

    if (m_VppDoUseList.size()) {
        INIT_MFX_EXT_BUFFER(m_VppDoUse, MFX_EXTBUFF_VPP_DOUSE);
        m_VppDoUse.NumAlg = (mfxU32)m_VppDoUseList.size();
        m_VppDoUse.AlgList = &m_VppDoUseList[0];

        m_VppExtParams.insert(m_VppExtParams.begin(), (mfxExtBuffer *)&m_VppDoUse);
        for (const auto& extParam : m_VppDoUseList) {
            PrintMes(RGY_LOG_DEBUG, _T("CreateVppExtBuffers: set DoUse %s.\n"), fourccToStr(extParam).c_str());
        }
    }

    //Haswell以降では、DONOTUSEをセットするとdetail enhancerの効きが固定になるなど、よくわからない挙動を示す。
    if (m_VppDoNotUseList.size() > 0 && getCPUGen(&m_mfxSession) < CPU_GEN_HASWELL) {
        INIT_MFX_EXT_BUFFER(m_VppDoNotUse, MFX_EXTBUFF_VPP_DONOTUSE);
        m_VppDoNotUse.NumAlg = (mfxU32)m_VppDoNotUseList.size();
        m_VppDoNotUse.AlgList = &m_VppDoNotUseList[0];
        m_VppExtParams.push_back((mfxExtBuffer *)&m_VppDoNotUse);
        for (const auto& extParam : m_VppDoNotUseList) {
            PrintMes(RGY_LOG_DEBUG, _T("CreateVppExtBuffers: set DoNotUse %s.\n"), fourccToStr(extParam).c_str());
        }
    }

    m_mfxVppParams.ExtParam = (m_VppExtParams.size()) ? &m_VppExtParams[0] : nullptr;
    m_mfxVppParams.NumExtParam = (mfxU16)m_VppExtParams.size();

    return RGY_ERR_NONE;
}

std::vector<VppType> QSVVppMfx::GetVppList() const {
    std::set<VppType> vppList;

    // colorspace
    if (   m_mfxVppParams.vpp.In.FourCC != m_mfxVppParams.vpp.Out.FourCC
        || m_mfxVppParams.vpp.In.BitDepthLuma != m_mfxVppParams.vpp.Out.BitDepthLuma
        || m_mfxVppParams.vpp.In.BitDepthChroma != m_mfxVppParams.vpp.Out.BitDepthChroma) {
        vppList.insert(VppType::MFX_COLORSPACE);
    }

    // resize
    if (   m_mfxVppParams.vpp.Out.CropW != m_mfxVppParams.vpp.In.CropW
        || m_mfxVppParams.vpp.Out.CropH != m_mfxVppParams.vpp.In.CropH) {
        vppList.insert(VppType::MFX_RESIZE);
    }

    // crop
    if (cropEnabled(m_crop)) {
        vppList.insert(VppType::MFX_CROP);
    }

    // deinterlace
    if (m_mfxVppParams.vpp.In.PicStruct != m_mfxVppParams.vpp.Out.PicStruct
        && m_mfxVppParams.vpp.Out.PicStruct == MFX_PICSTRUCT_PROGRESSIVE) {
        vppList.insert(VppType::MFX_DEINTERLACE);
    }

    // その他のフィルタ
    for (auto& vpp : m_VppDoUseList) {
        const auto type = vpp_extbuff_to_rgy(vpp);
        if (type != VppType::VPP_NONE) {
            vppList.insert(type);
        }
    }
    return std::vector<VppType>(vppList.begin(), vppList.end());
}

FrameInfo QSVVppMfx::GetFrameOut() const {
    const auto& mfxOut = m_mfxVppParams.vpp.Out;

    FrameInfo info;
    info.width = mfxOut.CropW;
    info.height = mfxOut.CropH;
    info.csp = csp_enc_to_rgy(mfxOut.FourCC);
    info.bitdepth = (mfxOut.BitDepthLuma > 0) ? mfxOut.BitDepthLuma : 8;
    info.picstruct = picstruct_enc_to_rgy(mfxOut.PicStruct);
    info.mem_type = (m_memType != SYSTEM_MEMORY) ? RGY_MEM_TYPE_GPU_IMAGE_NORMALIZED : RGY_MEM_TYPE_CPU;
    return info;
}

rgy_rational<int> QSVVppMfx::GetOutFps() const {
    const auto& mfxOut = m_mfxVppParams.vpp.Out;
    return rgy_rational<int>(mfxOut.FrameRateExtN, mfxOut.FrameRateExtD);
}

void QSVVppMfx::PrintMes(int log_level, const TCHAR *format, ...) {
    if (m_log.get() == nullptr) {
        if (log_level <= RGY_LOG_INFO) {
            return;
        }
    } else if (log_level < m_log->getLogLevel()) {
        return;
    }

    va_list args;
    va_start(args, format);

    int len = _vsctprintf(format, args) + 1; // _vscprintf doesn't count terminating '\0'
    vector<TCHAR> buffer(len, 0);
    _vstprintf_s(buffer.data(), len, format, args);
    va_end(args);

    tstring mes = tstring(_T("MFXVPP: ")) + buffer.data();

    if (m_log.get() != nullptr) {
        m_log->write(log_level, mes.c_str());
    } else {
        _ftprintf(stderr, _T("%s"), mes.c_str());
    }
}

int QSVVppMfx::clamp_param_int(int value, int low, int high, const TCHAR *param_name) {
    auto value_old = value;
    value = clamp(value, low, high);
    if (value != value_old) {
        PrintMes(RGY_LOG_WARN, _T("%s value changed %d -> %d, must be in range of %d-%d\n"), param_name, value_old, value, low, high);
    }
    return value;
}

RGY_ERR QSVVppMfx::CheckParamList(int value, const CX_DESC *list, const char *param_name) {
    for (int i = 0; list[i].desc; i++)
        if (list[i].value == value)
            return RGY_ERR_NONE;
    PrintMes(RGY_LOG_ERROR, _T("%s=%d, is not valid param.\n"), param_name, value);
    return RGY_ERR_INVALID_VIDEO_PARAM;
}

void QSVVppMfx::vppExtAddMes(const tstring& str) {
    VppExtMes += str;
    PrintMes(RGY_LOG_DEBUG, _T("SetVppExtBuffers: %s"), str.c_str());
};

int QSVVppMfx::logTemporarilyIgnoreErrorMes() {
    //MediaSDK内のエラーをRGY_LOG_DEBUG以下の時以外には一時的に無視するようにする。
    //RGY_LOG_DEBUG以下の時にも、「無視できるエラーが発生するかもしれない」ことをログに残す。
    const auto log_level = m_log->getLogLevel();
    if (log_level >= RGY_LOG_MORE) {
        m_log->setLogLevel(RGY_LOG_QUIET); //一時的にエラーを無視
    } else {
        PrintMes(RGY_LOG_DEBUG, _T("There might be error below, but it might be internal error which could be ignored.\n"));
    }
    return log_level;
}
