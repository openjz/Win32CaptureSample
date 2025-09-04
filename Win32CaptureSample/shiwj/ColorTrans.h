#pragma once
#include <mfapi.h>
#include <atlbase.h>
#include <Codecapi.h>
#include <wmcodecdsp.h> 
#include <mftransform.h>

struct ColorTransParam
{
	unsigned int src_width;
	unsigned int src_height;
	unsigned int dst_width;
	unsigned int dst_height;
};
class ColorTrans
{
public:
	ColorTrans();
	~ColorTrans();

	bool InitTrans(ColorTransParam *colorParam);
private:
	ColorTransParam m_colorParam;
	CComPtr<IMFTransform> m_transform;

};

