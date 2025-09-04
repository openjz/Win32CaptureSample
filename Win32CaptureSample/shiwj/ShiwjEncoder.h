#pragma once

#include "../pch.h"
#include <mftransform.h>
#include <strmif.h>

#include <cstdint>

#include "thirdparty/libmp4v2/2.0/include/mp4v2/general.h"

#include "Preproc.h"

namespace shiwj {

	struct  ST_DeskEncodeParam {
		unsigned int frameRate;
		unsigned long bitRate;
		unsigned int  gop;
		unsigned int  quality;
	};

	struct EncoderParam
	{
		unsigned int width;
		unsigned int height;
		unsigned int fps;
		unsigned long bitRate;
		unsigned int  gop;
		unsigned int  quality;
	};

	struct st_record
	{
		int width;
		int height;
		int fps;
		std::string filepath; // 视频全路径
		bool inputAudio;     // 是否录制输入音频
		bool outputAudio;    // 是否录制输出音频
		MP4FileHandle mp4Handle;
		MP4TrackId		mp4AudioInTrack;
		MP4TrackId		mp4AudioOutTrack;
		uint64_t endTs;       //结束时间戳
	};

	struct st_video
	{
		unsigned char* pdata;
		int len;
		unsigned long   uiTime;
		bool           isIDR;
	};

	class CMFEncoder {
	public:
		~CMFEncoder() {}
		int Init(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device, 
			int width, int height, int fps, const char* filePath, bool inputAudio, bool outputAudio);
		int EncodeFrame(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame& frame);
		void Close();
	private:
		int CreateEncoder();
		int SetInputOutPut();
		int SetInputType();
		int SetOutputType();

		void SetEncodeCallback(std::function<void(unsigned char*, int, unsigned long long, bool, bool, int, void*)> callback, void* pArg);
		void EncodeCallback(unsigned char* data, int len, unsigned long long time, bool invalid, bool iskey, int Iinvalid, void* parg)

		std::unique_ptr<EncoderParam> m_encoderParam = nullptr;
		UINT m_resetToken;
		winrt::com_ptr<IMFDXGIDeviceManager> m_deviceManager{ nullptr };
		HANDLE m_writeMp4Event = NULL;
		st_record  m_recordInfo;
		winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device{nullptr};
		winrt::com_ptr<ID3D11Device> m_d3dDevice{ nullptr };
		winrt::com_ptr<ID3D11DeviceContext> m_d3dContext{ nullptr };
		std::unique_ptr<RGBToNV12> m_colorConv = nullptr;
		winrt::com_ptr<IMFTransform> m_transform{nullptr};
		winrt::com_ptr<IMFActivate>  m_mfActive = nullptr;
		winrt::com_ptr<IMFMediaEventGenerator> m_eventGen{nullptr};
		DWORD m_inputStreamID = -1;
		DWORD m_outputStreamID = -1;
		winrt::com_ptr<ICodecAPI> mpCodecAPI = nullptr;
		winrt::com_ptr<ID3D11Texture2D> scaleTexture = nullptr;
		winrt::Windows::Graphics::SizeInt32 scaleTextureSize;

		std::function<void(unsigned char*, int, unsigned long long timesstamp, bool, bool, int, void*)>  m_callbackFun = nullptr;
		void* m_pArg = nullptr;
	};
}
