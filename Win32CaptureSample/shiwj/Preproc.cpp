
//#include "Defs.h"
#include "../pch.h"

#include "ShiwjCommon.h"
#include "Preproc.h"
//#include <dxvahd.h>
#define SAFE_RELEASE(X) if(X){X->Release(); X=nullptr;}
/// Constructor
RGBToNV12::RGBToNV12(ID3D11Device *pDev, ID3D11DeviceContext *pCtx)
    : m_pDev(pDev)
    , m_pCtx(pCtx)
{
    m_pDev->AddRef();
    m_pCtx->AddRef();
}

/// Initialize Video Context
HRESULT RGBToNV12::Init()
{
    /// Obtain Video device and Video device context
    HRESULT hr = m_pDev->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&m_pVid);
    if (FAILED(hr))
    {
		PLOG(plog::error) << L"QAI for ID3D11VideoDevice";
    }
    hr = m_pCtx->QueryInterface(__uuidof(ID3D11VideoContext), (void**)&m_pVidCtx);
    if (FAILED(hr))
    {
        PLOG(plog::error) << L"QAI for ID3D11VideoContext";
    }

    return hr;
}

/// Release all Resources
void RGBToNV12::Cleanup()
{
    for (auto& it : viewMap)
    {
        ID3D11VideoProcessorOutputView* pVPOV = it.second;
        pVPOV->Release();
    }
    viewMap.clear();
    SAFE_RELEASE(m_pVP);
    SAFE_RELEASE(m_pVPEnum);
    SAFE_RELEASE(m_pVidCtx);
    SAFE_RELEASE(m_pVid);
    SAFE_RELEASE(m_pCtx);
    SAFE_RELEASE(m_pDev);
}

/// Perform Colorspace conversion
HRESULT RGBToNV12::Convert(ID3D11Texture2D* pRGB, ID3D11Texture2D*pYUV, int nWidth, int nHeight, int srcX, int srcY)
{
    HRESULT hr = S_OK;
    ID3D11VideoProcessorInputView* pVPIn = nullptr;

    D3D11_TEXTURE2D_DESC inDesc = { 0 };
    D3D11_TEXTURE2D_DESC outDesc = { 0 };
    pRGB->GetDesc(&inDesc);
    if (0 != nWidth)
    {
        inDesc.Width = nWidth;
    }
    if (0 != nHeight)
    {
        inDesc.Height = nHeight;
    }

    
    pYUV->GetDesc(&outDesc);

    /// Check if VideoProcessor needs to be reconfigured
    /// Reconfiguration is required if input/output dimensions have changed
    if (m_pVP)
    {
        if (m_inDesc.Width != inDesc.Width ||
            m_inDesc.Height != inDesc.Height ||
            m_outDesc.Width != outDesc.Width ||
            m_outDesc.Height != outDesc.Height)
        {
            SAFE_RELEASE(m_pVPEnum);
            SAFE_RELEASE(m_pVP);
        }
    }

    if (!m_pVid || !m_pVidCtx)
    {
        PLOG(plog::error) << L"!m_pVid || !m_pVidCtx";
        hr = S_FALSE;
        return hr;
    }

    if (!m_pVP)
    {
        /// Initialize Video Processor
        m_inDesc = inDesc;
        m_outDesc = outDesc;
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc =
        {
            D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE,
            { 1, 1 }, inDesc.Width, inDesc.Height,
            { 1, 1 }, outDesc.Width, outDesc.Height,
            D3D11_VIDEO_USAGE_OPTIMAL_QUALITY
        };  // D3D11_VIDEO_USAGE_PLAYBACK_NORMAL tianyw0722
        hr = m_pVid->CreateVideoProcessorEnumerator(&contentDesc, &m_pVPEnum);
        if (FAILED(hr))
        {
            PLOG(plog::error) << L"CreateVideoProcessorEnumerator";
        }

        UINT uiFlags = 0;
       /* DXGI_FORMAT VP_Output_Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        m_pVPEnum->CheckVideoProcessorFormat(VP_Output_Format, &uiFlags);*/
      /*  uiFlags = 0;
        DXGI_FORMAT VP_input_Format = DXGI_FORMAT_NV12;
        m_pVPEnum->CheckVideoProcessorFormat(VP_input_Format, &uiFlags);*/
      
        hr = m_pVid->CreateVideoProcessor(m_pVPEnum, 0, &m_pVP);
        if (FAILED(hr))
        {

            PLOG(plog::error) << L"CreateVideoProcessor";
            return hr;
        }
        
    }


    if (m_srcX!= srcX ||
        m_srcY != srcY ||
        m_srcW != nWidth ||
        m_srcH != nHeight)
    {
        m_srcX = srcX;
        m_srcY = srcY;
        m_srcW = nWidth;
        m_srcH = nHeight;
        RECT srcrc{ srcX,srcY, m_inDesc.Width, m_inDesc.Height };
        m_pVidCtx->VideoProcessorSetStreamSourceRect(m_pVP, 0, TRUE, &srcrc);
    }
    
    /*if (srcX != m_srcX || srcY != m_srcY)
    {
        m_srcX = srcX;
        m_srcY = srcY;
        RECT srcrc{ srcX,srcY, m_inDesc.Width, m_inDesc.Height};
        m_pVidCtx->VideoProcessorSetStreamSourceRect(m_pVP, 0, TRUE, &srcrc);
    }*/
    /// Obtain Video Processor Input view from input texture
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputVD = { 0, D3D11_VPIV_DIMENSION_TEXTURE2D,{ 0,0 } };
    hr = m_pVid->CreateVideoProcessorInputView(pRGB, m_pVPEnum, &inputVD, &pVPIn);
    if (FAILED(hr))
    {
        PLOG(plog::error) << L"CreateVideoProcessInputView: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec;
        return hr;
    }
    /// Obtain Video Processor Output view from output texture
    ID3D11VideoProcessorOutputView* pVPOV = nullptr;
    auto it = viewMap.find(pYUV);
    /// Optimization: Check if we already created a video processor output view for this texture
    if (it == viewMap.end())
    {
        /// We don't have a video processor output view for this texture, create one now.
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ovD = { D3D11_VPOV_DIMENSION_TEXTURE2D };
        hr = m_pVid->CreateVideoProcessorOutputView(pYUV, m_pVPEnum, &ovD, &pVPOV);
        if (FAILED(hr))
        {
            SAFE_RELEASE(pVPIn);
            PLOG(plog::error) << L"CreateVideoProcessorOutputView: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec;
            return hr;
        }
        viewMap.insert({ pYUV, pVPOV });
    }
    else
    {
        pVPOV = it->second;
    }

    /// Create a Video Processor Stream to run the operation
    D3D11_VIDEO_PROCESSOR_STREAM stream = { TRUE, 0, 0, 0, 0, nullptr, pVPIn, nullptr };

    /// Perform the Colorspace conversion
    hr = m_pVidCtx->VideoProcessorBlt(m_pVP, pVPOV, 0, 1, &stream);
    if (FAILED(hr))
    {
        SAFE_RELEASE(pVPIn);
        PLOG(plog::error) << L"VideoProcessorBlt: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec;
        return hr;
    }
    SAFE_RELEASE(pVPIn);
    return hr;
}

