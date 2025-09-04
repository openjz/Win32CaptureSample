#pragma once
#ifdef DESKCAPENCODER_EXPORTS
#define DESKCAPENCODER_API __declspec(dllexport)
#else
#define DESKCAPENCODER_API __declspec(dllimport)
#endif
#include <memory>
#include <functional>
typedef enum
{
	CAP_MAINDISPLAY = 1,
	CAP_WINDOW,
}EM_CAPTRUETYPE;
struct  ST_DeskCaptrueParam
{
	EM_CAPTRUETYPE capType;      
	HWND capHwnd;
	HWND previewWnd;
	unsigned int videoWidth;
	unsigned int videoHeight;
};
struct  ST_DeskEncodeParam
{
	unsigned int frameRate;
	unsigned long bitRate;
	unsigned int  gop;
	unsigned int  quality;
};

class IDeskCapEncoderNotify
{
public:
	virtual void  OnImageDataRecv(unsigned char* data,int datasize,const char* type, uint64_t timestamp) = 0;
	virtual void  OnInit(int errcode, char* msg) = 0;
	virtual void  OnUnInit(int errcode, char* msg) = 0;
	virtual void  OnStartCapture(int errcode, char* msg) = 0;
	virtual void  OnStopCapture(int errcode, char* msg) = 0;
	virtual void  OnStartReplayBuffer(int errcode, char* msg) = 0;
	virtual void  OnSaveReplayBuffer(int errcode, char* msg,unsigned int  contextId,const char* file, uint64_t *hlOffsetMs,int hlOffsetMsCount) = 0;
	virtual void  OnStopReplayBuffer(int errcode, char* msg) = 0;
	virtual void  OnStartRecord(int errcode, char* msg, uint64_t  startTs) = 0;
	virtual void  OnStopRecord(int errcode, char* msg, const char* file, uint64_t  endTs) = 0;
	virtual void  OnStartManualRecord(int errcode, char* msg, uint64_t  startTs) = 0;
	virtual void  OnStopManualRecord(int errcode, char* msg, const char* file, uint64_t  endTs) = 0;
};
class DeskCapEncoderInterface
{
public:
	//virtual bool Init(ST_DeskCaptrueParam *capParam, ST_DeskEncodeParam *encoderParam) = 0;
	//virtual bool SetCoderDataCallback(std::function<void(unsigned char*,int, unsigned long long,bool,bool,int,void*)> callback,void *pArg) = 0;
	//virtual void SetForceKeyFrame() =0;
	//virtual void SetCaptrueParam(int width, int height, int frameRate = 0)=0;  // =0 ²ÎÊýºöÂÔ
	//virtual void SetCheckResule(unsigned long ultime) = 0;
	//virtual bool Close() = 0;
	//virtual bool Start() = 0;
	virtual void Init(IDeskCapEncoderNotify* notifier) = 0;
	virtual void UnInit() = 0;
	virtual void StartCapture(uint64_t handle, const char* tiltle, const char* ameExeName, int gameType, int internal, int captureType,bool isCaptureWithCursor,bool isCaptureWithBorder,int width,int height)=0;
	virtual void StopCapture()=0;
	virtual void StartReplayBuffer(int width, int height, int fps, char* recordPath, char* highPrefix, int hlBeforeSec, int hlAfterSec, int hlMaxSec, bool isCaptureWithCursor, bool isCaptureAudio,bool outputAudio) = 0;
	virtual void SaveReplayBuffer(unsigned int contextId, uint64_t* hlFramesTimestamps, int nCount,int index) = 0;
	virtual void StopReplayBuffer()=0;
	virtual void StartRecord(int weight, int height, int fps,  const char* filePath, bool inputAudio, bool outputAudio) = 0;
	virtual void StopRecord() = 0;
	virtual void StartManualRecord(int width, int height, int fps, uint64_t handle, const char* tiltle, int captureType,
		char* filePath, bool isCaptureWithCursor, bool isCaptureWithBorder, bool inputAudio, bool outputAudio)=0;
	virtual void StopManualRecord() = 0;
};
DESKCAPENCODER_API std::shared_ptr<DeskCapEncoderInterface> CreateDeskCapEncoder();