#pragma once

#include "../pch.h"
#include <mftransform.h>
#include <strmif.h>

#include <cstdint>

#include "thirdparty/libmp4v2/2.0/include/mp4v2/general.h"

#include "Preproc.h"
#include "Mp4Encoder.h"

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

	struct st_audio
	{
		unsigned char* pdata;
		int len;
		unsigned long   uiTime;
	};

	enum class EncodeEvent
	{
		NeedInput = 0,
	};

	class CMFEncoder {
	public:
		using EventCBFunc = std::function<void(EncodeEvent)>;
		~CMFEncoder();
		int Start(EventCBFunc eventCbFunc,
			winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device, 
			int width, int height, int fps, const char* filePath, bool inputAudio, bool outputAudio);
		int EncodeFrame(winrt::com_ptr<ID3D11Texture2D> frameTexture);
		void Close();
	private:
		int CreateEncoder();
		int SetOutputType();
		int SetInputType();

		void SetEncodeCallback(std::function<void(unsigned char*, int, unsigned long long, bool, bool, int, void*)> callback, void* pArg);
		void EncodeCallback(unsigned char* data, int len, unsigned long long time, bool invalid, bool iskey, int Iinvalid, void* parg);

		int StartEncoder();
		void EncodeThread();

		bool IsIDRSample(unsigned char* data, int len);

		void RecordFileFun();

		void StopEncoder();

		std::unique_ptr<EncoderParam> m_encoderParam = nullptr;
		UINT m_resetToken;
		winrt::com_ptr<IMFDXGIDeviceManager> m_deviceManager{ nullptr };
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
		winrt::com_ptr<ICodecAPI> m_codecAPI = nullptr;
		winrt::com_ptr<ID3D11Texture2D> scaleTexture = nullptr;
		winrt::Windows::Graphics::SizeInt32 scaleTextureSize;

		std::function<void(unsigned char*, int, unsigned long long timesstamp, bool, bool, int, void*)>  m_callbackFun = nullptr;
		void* m_pArg = nullptr;

		std::list<st_video>  m_videoList;
		CRITICAL_SECTION m_cs;
		std::mutex m_audioInLock;
		std::list<st_audio>  m_audioInList;
		int m_cacheMaxSec = 120 * 1000;//最大缓存时长
		std::list<st_audio>  m_audioOutList;
		std::mutex m_audioOutLock;

		std::thread m_encoderThread;
		long m_frameTime;
		std::atomic<bool>  m_closed = false;

		std::mutex m_d3d11textureLock;
		winrt::com_ptr<ID3D11Texture2D> m_d3d11texture{ nullptr };	//todo: 获取自EncodeFrame

		MP4Encoder m_mp4Encoder;
		std::thread thRecord;
		bool  m_bRecord = false;
		int m_nTimeScale = 90000;  //mp4 一般90000

		EventCBFunc m_eventCbFunc;
	};
}
