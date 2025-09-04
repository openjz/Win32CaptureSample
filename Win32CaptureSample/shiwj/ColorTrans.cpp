#include "pch.h"
#include "ColorTrans.h"

ColorTrans::ColorTrans()
{
	
}

ColorTrans::~ColorTrans()
{

}

bool ColorTrans::InitTrans(ColorTransParam* colorParam)
{
	bool bRet = false;
	HRESULT hr;
	//hr = CoCreateInstance(CLSID_CColorConvertDMO, NULL, CLSCTX_ALL, IID_PPV_ARGS(&m_transform));

	//CComPtr<IMFMediaType> inputType;
	//hr = MFCreateMediaType(&inputType);
	//hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	//hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	//hr = inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	//hr = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, colorParam->src_width, colorParam->src_height);
	//hr = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, 60, 1);
	//hr = MFSetAttributeRatio(inputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

	return bRet;
}
