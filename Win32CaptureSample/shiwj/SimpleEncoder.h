#pragma once
#include <strmif.h>
#include <mfapi.h>
#include <atlbase.h>
#include <Codecapi.h>
#include <mftransform.h>
#include <functional>
#include <thread>
#include "Preproc.h"
#include <mutex>

struct ST_DeskEncodeParam;
struct EncoderParam
{
	unsigned int width;
	unsigned int height;
	unsigned int fps;
	unsigned long bitRate;
	unsigned int  gop;
	unsigned int  quality;
//	ST_DeskEncodeParam *deskEncodeParam;
};
class SimpleCapture;
class SimpleEncoder
{
public:
	SimpleEncoder(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device, CComPtr<IMFDXGIDeviceManager> deviceManager);
	~SimpleEncoder();
	bool InitEncoder(EncoderParam* encoderParam, std::shared_ptr<SimpleCapture> &capture);
	bool SetCoderDataCallback(std::function<void(unsigned char*, int, unsigned long long, bool, bool, int, void*)> callback, void* pArg);
	bool StartEncoder();
	bool StopEncoder();
	void SetForceKeyFrame();
	void SetCaptrueParam(int width, int height, int frameRate = 0);
	void Stop();
private:
	bool SetInputType();
	bool SetOutputType();
	void DecoderThread();

	bool SetInputOutPut(int timeCount);

	void SendDeskResolution(int nWidth, int nHeight, bool isBPG);
	void ResetParam();
	
	void Start();
	void UnInitEncoder();
	bool IsIDRSample(unsigned char* data, int len);
private:
	winrt::com_ptr<ID3D11Device> m_device;
	winrt::com_ptr<ID3D11DeviceContext> m_d3dContext{ nullptr };
	EncoderParam* m_encoderParam = nullptr;

	ICodecAPI *mpCodecAPI = nullptr;

	CComPtr<IMFActivate>  m_mfActive = nullptr;

	std::shared_ptr<SimpleCapture> m_capture=nullptr;
	CComPtr<IMFTransform> m_transform;
	CComQIPtr<IMFMediaEventGenerator> m_eventGen;
	CComPtr<IMFDXGIDeviceManager> m_deviceManager;
	DWORD m_inputStreamID=-1;
	DWORD m_outputStreamID=-1;
	std::function<void(unsigned char*, int, unsigned long long timesstamp, bool, bool, int, void*)>  m_callbackFun = nullptr;
	void* m_pArg = nullptr;
	std::thread m_encoderThread;
	std::atomic<bool>  m_closed = false;
	//std::mutex  m_encoderMutex;
	std::unique_ptr<RGBToNV12>   m_colorConv = nullptr;
	ID3D11Texture2D* scaleTexture = nullptr;
	winrt::Windows::Graphics::SizeInt32 scaleTextureSize;
	bool m_bIsAmd = false;
	bool m_bIsInvada = false;
	int  m_width=0;
	int  m_height=0;
	long m_frameTime;
	int  m_frameRate = 1;
	
};

