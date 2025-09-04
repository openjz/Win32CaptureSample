#include "pch.h"
#include "MP4Encoder.h"
#include <string.h>
#include <Windows.h>
#define BUFFER_SIZE  (1024*1024 *10)

static int GetSRIndex(unsigned int sampleRate)
{
	if (92017 <= sampleRate)return 0;
	if (75132 <= sampleRate)return 1;
	if (55426 <= sampleRate)return 2;
	if (46009 <= sampleRate)return 3;
	if (37566 <= sampleRate)return 4;
	if (27713 <= sampleRate)return 5;
	if (23004 <= sampleRate)return 6;
	if (18783 <= sampleRate)return 7;
	if (13856 <= sampleRate)return 8;
	if (11502 <= sampleRate)return 9;
	if (9391 <= sampleRate)return 10;

	return 11;
}

static short getAudioConfig(unsigned int sampleRate, unsigned int channels)
{
	/* 参考: https://my.oschina.net/u/1177171/blog/494369
	 * [5 bits AAC Object Type] [4 bits Sample Rate Index] [4 bits Channel Number] [3 bits 0]
	 */

#define	MAIN 1
#define	LOW  2
#define	SSR  3
#define	LTP  4

	return (LOW << 11) | (GetSRIndex(sampleRate) << 7) | (channels << 3);
}
#pragma comment(lib, "lib/libmp4v2.lib")
MP4Encoder::MP4Encoder(void) :
	m_videoId(NULL),
	m_nWidth(0),
	m_nHeight(0),
	m_nTimeScale(0),
	m_nFrameRate(0)
{
}

MP4Encoder::~MP4Encoder(void)
{
}

MP4FileHandle MP4Encoder::OpenMP4File(const char* fileName)
{
	if (fileName == NULL)
	{
		return 0;
	}

	
	// create mp4 file
	MP4FileHandle hMp4file = MP4Modify(fileName);
	if (hMp4file == MP4_INVALID_FILE_HANDLE)
	{
		LOG_INFO(L"ERROR:Open file fialed.\n");
		return 0;
	}
	return hMp4file;
}
MP4FileHandle MP4Encoder::CreateMP4File(const char *pFileName, int width, int height, int timeScale/* = 90000*/, int frameRate/* = 25*/)
{
	if (pFileName == NULL)
	{
		return 0;
	}
	// create mp4 file
	//MP4FileHandle hMp4file = MP4Create(pFileName);
	MP4FileHandle hMp4file = MP4CreateEx(pFileName, 0, 1, 1, 0, 0, 0, 0);

	if (hMp4file == MP4_INVALID_FILE_HANDLE)
	{
		LOG_INFO(L"ERROR:Open file fialed.\n");
		return 0;
	}
	m_nWidth = width;
	m_nHeight = height;
	m_nTimeScale = timeScale; // 1200000;
	m_nFrameRate = frameRate;
	MP4SetTimeScale(hMp4file, m_nTimeScale);
	fgNeedtoAddTrack = 1;
	return hMp4file;
}

bool MP4Encoder::Write264Metadata(MP4FileHandle hMp4File, LPMP4ENC_Metadata lpMetadata)
{
	m_videoId = MP4AddH264VideoTrack
	(hMp4File,
		m_nTimeScale,
		m_nTimeScale / m_nFrameRate,
		m_nWidth, // width
		m_nHeight,// height
		lpMetadata->Sps[1], // sps[1] AVCProfileIndication
		lpMetadata->Sps[2], // sps[2] profile_compat
		lpMetadata->Sps[3], // sps[3] AVCLevelIndication
		3);           // 4 bytes length before each NAL unit
	if (m_videoId == MP4_INVALID_TRACK_ID)
	{
		LOG_INFO(L"add video track failed.\n");
		return false;
	}
	MP4SetVideoProfileLevel(hMp4File, 0x01); //  Simple Profile @ Level 3

											 // write sps
	MP4AddH264SequenceParameterSet(hMp4File, m_videoId, lpMetadata->Sps, lpMetadata->nSpsLen);

	// write pps
	MP4AddH264PictureParameterSet(hMp4File, m_videoId, lpMetadata->Pps, lpMetadata->nPpsLen);
	return true;
}

MP4TrackId MP4Encoder::WriteAACMetadata(MP4FileHandle hMp4File,uint32_t audio_sample,uint16_t channels)
{
	/* MP4操作 4/8：添加音频track，设置音频格式 */
	MP4TrackId audioTrackId = MP4AddAudioTrack(hMp4File, audio_sample, 1024,
		MP4_MPEG4_AUDIO_TYPE);
	if (audioTrackId == MP4_INVALID_TRACK_ID)
	{
		LOG_INFO(L"add aac audio track error!\n");
		return MP4_INVALID_TRACK_ID;
	}
	MP4SetAudioProfileLevel(hMp4File, 0x1); // 0x02 ==> MPEG4 AAC LC

	/* MP4操作 5/8：根据音频协议、采样率、声道设置音频参数 */
	// 推荐都填充上，否则部分播放器播放时没有声音，配置参数有两种方式获取：
	//  - 从开源项目faac的`faacEncGetDecoderSpecificInfo`函数获取；
	//  - 我们自己实现了一个，这样可以避免依赖于其他项目的程序代码。<=
	short audioConfig = getAudioConfig(audio_sample, 1);
	audioConfig = ((audioConfig & 0x00ff) << 8) | ((audioConfig >> 8) & 0x00ff);
	MP4SetTrackESConfiguration(hMp4File, audioTrackId, (const uint8_t*)&audioConfig, 2);
	return audioTrackId;
}
int MP4Encoder::WriteH264DataEx(MP4FileHandle hMp4File, const unsigned char* pData, int size, uint64_t uiTime)
{
	mp4_video video;
	video.pdata = new unsigned char[size];
	video.uiTime = uiTime;
	memcpy_s(video.pdata,size, pData, size);
	video.len = size;
	m_videoVec.push_back(video);
	return 0;
}
int MP4Encoder::WriteH264Data(MP4FileHandle hMp4File, const unsigned char* pData, int size, uint64_t timestamp)
{
	if (hMp4File == NULL)
	{
		return -1;
	}
	if (pData == NULL)
	{
		return -1;
	}
	static uint64_t time = GetTickCount();
	MP4ENC_NaluUnit nalu;
	int pos = 0, len = 0;
	while (len = ReadOneNaluFromBuf(pData, size, pos, nalu))
	{
		if (nalu.type == 0x07) // sps
		{
			//static int fgNeedtoAddTrack = 1;
			// 添加h264 track
			
			if (fgNeedtoAddTrack)
			{
				m_videoId = MP4AddH264VideoTrack
				(hMp4File,
					m_nTimeScale,
					MP4_INVALID_DURATION,//m_nTimeScale / m_nFrameRate,
					m_nWidth,     // width
					m_nHeight,    // height
					nalu.data[1], // sps[1] AVCProfileIndication
					nalu.data[2], // sps[2] profile_compat
					nalu.data[3], // sps[3] AVCLevelIndication
					3);           // 4 bytes length before each NAL unit
				if (m_videoId == MP4_INVALID_TRACK_ID)
				{
					LOG_INFO(L"add video track failed.\n");
					return 0;
				}
				MP4SetVideoProfileLevel(hMp4File, 1); //  Simple Profile @ Level 3
				fgNeedtoAddTrack = false;
			}
			MP4AddH264SequenceParameterSet(hMp4File, m_videoId, nalu.data, nalu.size);
		}
		else if (nalu.type == 0x08) // pps
		{
			MP4AddH264PictureParameterSet(hMp4File, m_videoId, nalu.data, nalu.size);
		}
		else if (nalu.type == 0x06 || nalu.type == 0x09 || m_videoId == MP4_INVALID_TRACK_ID)  // 
		{
			//printf("m_videoId == MP4_INVALID_TRACK_ID");
		}
		else if (nalu.type == 0x05)
		{
			MP4Duration duration;
			if (m_lastVideoTimestamp == 0)
			{
				duration = 3600;
			}
			else
			{
				duration = (timestamp - m_lastVideoTimestamp) * m_nTimeScale / 1000;
				
			}
			m_lastVideoTimestamp = timestamp;
			int datalen = nalu.size + 4;
			unsigned char* data = new unsigned char[datalen];
			// MP4 Nalu前四个字节表示Nalu长度
			data[0] = nalu.size >> 24;
			data[1] = nalu.size >> 16;
			data[2] = nalu.size >> 8;
			data[3] = nalu.size & 0xff;
			memcpy_s(data + 4, datalen, nalu.data, nalu.size);
			if (!MP4WriteSample(hMp4File, m_videoId, data, datalen, duration, 0, 1))  // MP4_INVALID_DURATION
			{
				delete[] data;
				return 0;
			}
			delete[] data;
		}
		else if (nalu.type == 0x01)
		{
			MP4Duration duration;
			if (m_lastVideoTimestamp == 0)
			{
				duration = 3600;

			}
			else
			{
				duration = (timestamp - m_lastVideoTimestamp) * m_nTimeScale / 1000;
				
			}
			m_lastVideoTimestamp = timestamp;
			int datalen = nalu.size + 4;
			unsigned char *data = new unsigned char[datalen];
			// MP4 Nalu前四个字节表示Nalu长度
			data[0] = nalu.size >> 24;
			data[1] = nalu.size >> 16;
			data[2] = nalu.size >> 8;
			data[3] = nalu.size & 0xff;
			memcpy_s(data + 4, datalen, nalu.data, nalu.size);
			if (!MP4WriteSample(hMp4File, m_videoId, data, datalen, duration, 0, 0))  // MP4_INVALID_DURATION
			{
				delete[] data;
				return 0;
			}
			delete[] data;
		}

		pos += len;
	}
	return pos;
}
bool MP4Encoder::WreiteAACData(MP4FileHandle hMp4File, MP4TrackId audioTrackId,const unsigned char* pData, int size)
{
	
	MP4WriteSample(hMp4File, audioTrackId, pData, size, MP4_INVALID_DURATION, 0, 1);
	return true;
}
//int MP4Encoder::ReadOneNaluFromBuf(const unsigned char *buffer, unsigned int nBufferSize, unsigned int offSet, MP4ENC_NaluUnit &nalu)
//{
//	int i = offSet;
//	while (i<nBufferSize)
//	{
//		if (buffer[i++] == 0x00 &&
//			buffer[i++] == 0x00 &&
//			buffer[i++] == 0x00 &&
//			buffer[i++] == 0x01
//			)
//		{
//			int pos = i;
//			while (pos<nBufferSize)
//			{
//				if (buffer[pos++] == 0x00 &&
//					buffer[pos++] == 0x00 &&
//					buffer[pos++] == 0x00 &&
//					buffer[pos++] == 0x01
//					)
//				{
//					break;
//				}
//			}
//			if (pos == nBufferSize)
//			{
//				nalu.size = pos - i;
//			}
//			else
//			{
//				nalu.size = (pos - 4) - i;
//			}
//
//			nalu.type = buffer[i] & 0x1f;
//			nalu.data = (unsigned char*)&buffer[i];
//			return (nalu.size + i - offSet);
//		}
//	}
//	return 0;
//}

int MP4Encoder::ReadOneNaluFromBuf(const unsigned char *buffer, unsigned int nBufferSize, unsigned int offSet, MP4ENC_NaluUnit &nalu)
{
	int i = offSet;
	int step = 0;
	int step2 = 0;
	while (i<nBufferSize)
	{
		if ((buffer[i] == 0x00 && buffer[i + 1] == 0x00 && buffer[i + 2] == 0x00 && buffer[i + 3] == 0x01) || (buffer[i] == 0x00 && buffer[i + 1] == 0x00 && buffer[i + 2] == 0x01))
		{
			if (buffer[i + 2] == 0x01)
			{
				step = 3;
			}
			else
				step = 4;
			i += step;
			int pos = i;
			step2 = 0;
			while (pos < nBufferSize - 3)
			{
				if ((buffer[pos] == 0x00 && buffer[pos + 1] == 0x00 && buffer[pos + 2] == 0x00 && buffer[pos + 3] == 0x01) || (buffer[pos] == 0x00 && buffer[pos + 1] == 0x00 && buffer[pos + 2] == 0x01))
				{
					if (buffer[i + 2] == 0x01)
					{
						step2 = 3;
					}
					else
						step2 = 4;
					break;
				}
				pos++;
			}
			if (step2 == 0)
			{
				nalu.size = pos - i+3;
			}
			else
			{
				nalu.size = pos - i;
			}

			nalu.type = buffer[i] & 0x1f;
			nalu.data = (unsigned char*)&buffer[i];
			return (nalu.size + i - offSet);
		}
		else
			i++;
	}
	return 0;
}

void MP4Encoder::CloseMP4File(MP4FileHandle hMp4File)
{
	if (hMp4File)
	{
		if (m_videoVec.size()>0)
		{
			m_nFrameRate = m_videoVec.size() / ((m_videoVec.rbegin()->uiTime - m_videoVec.begin()->uiTime) / 1000);
		}
		
		for (size_t i = 0; i < m_videoVec.size(); i++)
		{
			mp4_video& video = m_videoVec.at(i);
			if (video.pdata)
			{
				WriteH264Data(hMp4File, video.pdata, video.len,video.uiTime);
				delete video.pdata;
				video.pdata = nullptr;
			}
		}
		
		m_videoVec.clear();

		MP4Close(hMp4File);
		hMp4File = NULL;
	}
	m_lastVideoTimestamp = 0;
}
//
//bool MP4Encoder::WriteH264File(const char* pFile264, const char* pFileMp4)
//{
//	if (pFile264 == NULL || pFileMp4 == NULL)
//	{
//		return false;
//	}
//
//	MP4FileHandle hMp4File = CreateMP4File(pFileMp4, 1920, 1080);  // 352, 288
//	//MP4FileHandle hMp4File = OpenMP4File(pFileMp4);
//
//	if (hMp4File == NULL)
//	{
//		printf("ERROR:Create file failed!");
//		return false;
//	}
//
//	FILE* fp = fopen(pFile264, "rb");
//	if (!fp)
//	{
//		printf("ERROR:open file failed!");
//		return false;
//	}
//	fseek(fp, 0, SEEK_SET);
//
//	unsigned char* buffer = new unsigned char[BUFFER_SIZE];
//	int pos = 0;
//	while (1)
//	{
//		int readlen = fread(buffer + pos, sizeof(unsigned char), BUFFER_SIZE - pos, fp);
//
//
//		if (readlen <= 0)
//		{
//			break;
//		}
//
//		readlen += pos;
//
//		int writelen = 0;
//		/*for (int i = readlen - 1; i >= 0; i--)
//		{
//			if (buffer[i--] == 0x01 &&
//				buffer[i--] == 0x00 &&
//				buffer[i--] == 0x00 &&
//				buffer[i--] == 0x00
//				)
//			{
//				writelen = i + 5;
//				break;
//			}
//		}*/
//
//		for (int i = readlen - 1; i >= 0; i--)
//		{
//			if (buffer[i] == 0x01 &&
//				buffer[i - 1] == 0x00 &&
//				buffer[i - 2] == 0x00 &&
//				buffer[i - 3] == 0x00
//				)
//			{
//				i -= 4;
//				writelen = i + 5;
//				break;
//			}
//
//			if (buffer[i] == 0x01 &&
//				buffer[i - 1] == 0x00 &&
//				buffer[i - 2] == 0x00
//				)
//			{
//				i -= 3;
//				writelen = i + 4;
//				break;
//			}
//
//		}
//
//
//		writelen = WriteH264Data(hMp4File, buffer, writelen);
//		if (writelen <= 0)
//		{
//			//	break;
//		}
//		memcpy_s(buffer, buffer + writelen, readlen - writelen + 0);  // tianyw +1
//		pos = readlen - writelen + 0;  // tianyw
//	}
//	fclose(fp);
//
//	delete[] buffer;
//	CloseMP4File(hMp4File);
//
//	return true;
//}


//bool MP4Encoder::WriteH264File(const char* pFile264, const char* pFileMp4)
//{
//	if (pFile264 == NULL || pFileMp4 == NULL)
//	{
//		return false;
//	}
//
//	MP4FileHandle hMp4File = CreateMP4File(pFileMp4, 1920, 1080);  // 352, 288
//	
//
//	if (hMp4File == NULL)
//	{
//		printf("ERROR:Create file failed!");
//		return false;
//	}
//
//	FILE *fp = fopen(pFile264, "rb");
//	if (!fp)
//	{
//		printf("ERROR:open file failed!");
//		return false;
//	}
//	fseek(fp, 0, SEEK_SET);
//
//	unsigned char *buffer = new unsigned char[BUFFER_SIZE];
//	int pos = 0;
//	int iStart = -1;
//	int iEnd = -1;
//	int i = 0;
//	while (1)
//	{
//		int readlen = fread(buffer + pos, sizeof(unsigned char), BUFFER_SIZE - pos, fp);
//
//
//		if (readlen <= 0)
//		{
//			break;
//		}
//
//		readlen += pos;
//
//		int writelen = 0;
//		/*for (int i = readlen - 1; i >= 0; i--)
//		{
//			if (buffer[i--] == 0x01 &&
//				buffer[i--] == 0x00 &&
//				buffer[i--] == 0x00 &&
//				buffer[i--] == 0x00
//				)
//			{
//				writelen = i + 5;
//				break;
//			}
//		}*/
//
//		
//		
//		if (iStart == -1)
//		{
//			for (i = 0; i < readlen; i++)
//			{
//				if (buffer[i] == 0x00 &&
//					buffer[i + 1] == 0x00 &&
//					buffer[i + 2] == 0x00 &&
//					buffer[i + 3] == 0x01
//					)
//				{
//					iStart = i;
//					break;
//				}
//
//				if (buffer[i] == 0x00 &&
//					buffer[i + 1] == 0x00 &&
//					buffer[i + 2] == 0x01
//					)
//				{
//					iStart = i;
//					break;
//				}
//			}
//			i += 3;
//		}
//		for (; i < readlen-3; i++)
//		{
//			if (buffer[i] == 0x00 &&
//				buffer[i + 1] == 0x00 &&
//				buffer[i + 2] == 0x00 &&
//				buffer[i + 3] == 0x01
//				)
//			{
//				iEnd = i;
//			}
//
//			/*if (buffer[i] == 0x00 &&
//				buffer[i + 1] == 0x00 &&
//				buffer[i + 2] == 0x01 
//				)
//			{
//				iEnd = i;
//			}*/
//			if (iEnd > 0)
//			{
//				writelen = WriteH264Data(hMp4File, buffer+iStart, iEnd - iStart);
//				if (writelen <= 0)
//				{
//					break;
//				}
//				iStart = iEnd;
//				iEnd = -1;
//				i += 2;
//			}
//		}
//		
//		memcpy_s(buffer, buffer + iStart, readlen - iStart + 0);  // tianyw +1
//		pos = readlen - iStart + 0;  // tianyw
//		i = 2;
//		iStart = 0;
//	}
//	fclose(fp);
//
//	delete[] buffer;
//	CloseMP4File(hMp4File);
//
//	return true;
//}

bool MP4Encoder::PraseMetadata(const unsigned char* pData, int size, MP4ENC_Metadata &metadata)
{
	if (pData == NULL || size<4)
	{
		return false;
	}
	MP4ENC_NaluUnit nalu;
	int pos = 0;
	bool bRet1 = false, bRet2 = false;
	while (int len = ReadOneNaluFromBuf(pData, size, pos, nalu))
	{
		if (nalu.type == 0x07)
		{
			memcpy_s(metadata.Sps,1024, nalu.data, nalu.size);
			metadata.nSpsLen = nalu.size;
			bRet1 = true;
		}
		else if ((nalu.type == 0x08))
		{
			memcpy_s(metadata.Pps,1024, nalu.data, nalu.size);
			metadata.nPpsLen = nalu.size;
			bRet2 = true;
		}
		pos += len;
	}
	if (bRet1 && bRet2)
	{
		return true;
	}
	return false;
}
