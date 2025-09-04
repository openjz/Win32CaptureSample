#pragma once
#include "DeskCapEncoderInterface.h"
#include "MonitorList.h"
#include "SimpleCapture.h"
#include "SimpleEncoder.h"
#include "MP4Encoder.h"
#include "CoreAudioInterface.h"

//#include <mfapi.h>
//#include <mfidl.h>
//#include <Mfreadwrite.h>
#include <list>
#include <vector>
//#pragma comment(lib, "mfreadwrite")
//#pragma comment(lib, "mfplat")
//#pragma comment(lib, "mfuuid")

//const UINT32 VIDEO_FPS = 30;
//const UINT64 VIDEO_FRAME_DURATION = 10 * 1000 * 1000 / VIDEO_FPS;
//const UINT32 VIDEO_BIT_RATE = 2000000;
//const GUID   VIDEO_ENCODING_FORMAT = MFVideoFormat_H264;
//const GUID   VIDEO_INPUT_FORMAT = MFVideoFormat_RGB32;
//const UINT32 VIDEO_FRAME_COUNT = 5 * VIDEO_FPS;

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

struct st_saveContext
{
	std::vector<uint64_t> frameTimes;//�߹�ʱ�̼���
	uint32_t contextId;//�洢����ID
};

struct st_media
{
	unsigned int  contextid;
	uint64_t  startTime;  //��ʼʱ��
	uint64_t  endTime;    //����ʱ��
	uint64_t  lenTime;    //ʱ�䳤��
	uint64_t  writeTime;  //���д�ļ�ʱ��
	std::string filename; //�����ļ���
	MP4FileHandle mp4Handle;
	//std::vector<uint64_t>  framesTimes;
	std::map < uint32_t, st_saveContext > saveContexts;
	MP4TrackId		mp4AudioInTrack;
	MP4TrackId		mp4AudioOutTrack;
	bool m_bSave;
};
struct st_record
{
	int width;
	int height;
	int fps;
	std::string filepath; // ��Ƶȫ·��
	bool inputAudio;     // �Ƿ�¼��������Ƶ
	bool outputAudio;    // �Ƿ�¼�������Ƶ
	MP4FileHandle mp4Handle;
	MP4TrackId		mp4AudioInTrack;
	MP4TrackId		mp4AudioOutTrack;
	uint64_t endTs;       //����ʱ���
};
class DeskCapEncoder : public DeskCapEncoderInterface
{
public:
	DeskCapEncoder();
	~DeskCapEncoder();
	//virtual bool Init(ST_DeskCaptrueParam* capParam, ST_DeskEncodeParam* encoderParam);
	//virtual bool SetCoderDataCallback(std::function<void(unsigned char*, int, unsigned long long, bool, bool, int, void*)> callback, void* pArg);
	//virtual void SetForceKeyFrame();
	//void SetCaptrueParam(int width, int height, int frameRate = 0);
	//void SetCheckResule(unsigned long ultime);
	//virtual bool Close();
	//virtual bool Start();

	void Init(IDeskCapEncoderNotify* notifier);
	void UnInit();
	void StartCapture(uint64_t handle, const char* tiltle, const char* ameExeName, int gameType, int internal, int captureType, bool isCaptureWithCursor, bool isCaptureWithBorder, int width, int height);
	void StopCapture();
	// ��ʼ��������
	void StartReplayBuffer(int width, int height, int fps, char* recordPath, char* highPrefix, int hlBeforeSec, int hlAfterSec, int hlMaxSec, bool isCaptureWithCursor, bool isCaptureAudio, bool outputAudio);
	void SaveReplayBuffer(unsigned int contextId, uint64_t* hlFramesTimestamps, int nCount, int index);
	void StopReplayBuffer();
	void StartRecord(int weight, int height, int fps,  const char* filePath, bool inputAudio, bool outputAudio);
	void StopRecord();
	void StartManualRecord(int width, int height, int fps, uint64_t handle, const char* tiltle, int captureType,
		char* filePath, bool isCaptureWithCursor, bool isCaptureWithBorder, bool inputAudio, bool outputAudio) override;
	void StopManualRecord() override;
public:
	void OnAudioDataCallBack(unsigned char* pOutData, int outDataLen, bool isEncode,void* pArg);
private:
	bool CreateDisplayCapture(bool isBorderRequest,bool isCurser,std::wstring ameExeName);
//	bool CreateWindowCapture(HWND hwnd);
	bool CreateEncoder();
	void CreateWndInThread(int nWidth, int nHeight);
	void CreatePreviewWndInThread(int nWidth, int nHeight);

	void EncoderCallback(unsigned char* data, int len, unsigned long long time, bool invalid, bool iskey, int Iinvalid, void* parg);
	void OutputVideoFun();
	void WriteFileFun();
	void RecordFileFun();
	void StopEncoder();
	void InnerStopCapture();
	
	void StopAudioCapture();
	void CreateD3DDevice();
	
private:
	ST_DeskCaptrueParam* m_captrueParam=nullptr;
	EncoderParam* m_encoderParam=nullptr;

	bool  m_bCaptureInit = false;
	bool  m_bEncoderInit = false;
	bool  m_bClose = true;
	//winrt::com_ptr<ID3D11Device> m_d3d11device;

	UINT resetToken;
	CComPtr<IMFDXGIDeviceManager> m_deviceManager{nullptr};
	//std::unique_ptr<WindowList> m_windows;
	std::unique_ptr<MonitorList> m_monitors;
	std::shared_ptr<SimpleCapture> m_capture = { nullptr };
	std::shared_ptr<SimpleEncoder> m_encoder = nullptr;
	HWND         m_hWnd = nullptr;

	HWND             m_previewHwnd = nullptr;
	//SimpleEncoder *m_encoder = nullptr;
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device{ nullptr };
	winrt::com_ptr<ID3D11Device> m_d3ddevice{ nullptr };
	std::list<st_video>  m_videoList;
	int  m_hlBeforeSec = 5000;  // ��ǰ����ʱ��Ĭ��5��
	int  m_hlAfterSec = 5000;  //  ��󱣴�ʱ��Ĭ��5��
	int  m_hlMaxSec = 10000;  //   ��Ƶ��󳤶�
	std::string  m_recordPath; //�ļ�·��
	std::string  m_highPrefix; //��Ƶ�ļ���ǰ׺
	int m_cacheMaxSec = 120 * 1000;//��󻺴�ʱ��

//	IMFSinkWriter* pSinkWriter = nullptr;
	std::vector<uint64_t>   m_framesTimesVec;  // �߹�ʱ���б�
	std::list<st_media>     m_mediaList;       // ��Ƶ�����б�
	
	bool       m_bSave = true;
	bool       m_bReplay = false;  // ������Ƶ
	IDeskCapEncoderNotify* m_notify = nullptr;
	int  m_captureType;
	int  m_fileIndex = 1;

	int m_nTimeScale = 90000;  //mp4 һ��9000
	MP4Encoder m_mp4Encoder;
	HANDLE m_writeMp4Event = NULL;
	std::shared_ptr<std::thread>  thWrite = nullptr;
	std::shared_ptr<std::thread>  thRecord = nullptr;
	std::mutex     m_medialstMutex;
	bool  m_bRecord = false;
	bool  m_bWrite = false;
	st_record  m_recordInfo;

	std::shared_ptr<CoreAudioCaptrueInterface> m_audioInCapture = nullptr;
	std::shared_ptr<CoreAudioCaptrueInterface> m_audioOutCapture = nullptr;
	//FILE* m_audioinFile = nullptr;
	std::list<st_audio>  m_audioInList;
	std::list<st_audio>  m_audioOutList;
	std::mutex m_audioOutLock;
	//std::mutex m_videoLock;
	std::mutex m_audioInLock;
	std::wstring m_monitorName=L"";
	CRITICAL_SECTION m_cs;
	bool m_isCaptureWithBorder = false;
	bool m_isCurser = false;
	std::string m_strTitle;
	int m_ninternal = 0;
	
};

