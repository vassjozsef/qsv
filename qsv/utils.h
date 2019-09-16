#pragma once

#include <list>
#include <string>
#include <vector>

#include "mfxstructures.h"

#define MSDK_SAFE_DELETE_ARRAY(P)                {if (P) {delete[] P; P = NULL;}}
#define MSDK_SAFE_DELETE(P)                      {if (P) {delete P; P = NULL;}}
#define MSDK_CHECK_POINTER(P, ...)               {if (!(P)) {return __VA_ARGS__;}}
#define MSDK_CHECK_ERROR(P, X, ERR)              {if ((X) == (P)) {return ERR;}}
#define MSDK_CHECK_STATUS(X, MSG)                {if ((X) < MFX_ERR_NONE) {std::cout << MSG << std::endl; return X;}}
#define MSDK_CHECK_STATUS_NO_RET(X, MSG)         {if ((X) < MFX_ERR_NONE) {std::cout << MSG << std::endl;}}
#define MSDK_ZERO_MEMORY(VAR)                    {memset(&VAR, 0, sizeof(VAR));}
#define MSDK_CHECK_STATUS_SAFE(X, FUNC, ADD)     {if ((X) < MFX_ERR_NONE) {ADD; std::cout << FUNC << std::endl; return X;}}
#define MSDK_MAX(A, B)                           (((A) > (B)) ? (A) : (B))
#define MSDK_MIN(A, B)                           (((A) < (B)) ? (A) : (B))
#define MSDK_ALIGN16(value)                      (((value + 15) >> 4) << 4) // round up to a multiple of 16
#define MSDK_ALIGN32(value)                      (((value + 31) >> 5) << 5) // round up to a multiple of 32
#define MSDK_IGNORE_MFX_STS(P, X)                {if ((X) == (P)) {P = MFX_ERR_NONE;}}
#define MSDK_BREAK_ON_ERROR(P)                   {if (MFX_ERR_NONE != (P)) break;}

#define MSDK_MEMCPY_VAR(dstVarName, src, count) memcpy_s(&(dstVarName), sizeof(dstVarName), (src), (count))

#define MSDK_DEC_WAIT_INTERVAL 300000
#define MSDK_ENC_WAIT_INTERVAL 300000
#define MSDK_VPP_WAIT_INTERVAL 300000
#define MSDK_SURFACE_WAIT_INTERVAL 20000
#define MSDK_DEVICE_FREE_WAIT_INTERVAL 30000
#define MSDK_WAIT_INTERVAL MSDK_DEC_WAIT_INTERVAL+3*MSDK_VPP_WAIT_INTERVAL+MSDK_ENC_WAIT_INTERVAL // an estimate for the longest pipeline we have in samples
#define MSDK_INVALID_SURF_IDX 0xFFFF
#define MSDK_SLEEP(msec) Sleep(msec)

enum {
	MFX_FOURCC_I420 = MFX_MAKEFOURCC('I', '4', '2', '0')
};

mfxStatus InitMfxBitstream(mfxBitstream* pBitstream, mfxU32 nSize);
mfxStatus ExtendMfxBitstream(mfxBitstream* pBitstream, mfxU32 nSize);
void WipeMfxBitstream(mfxBitstream* pBitstream);
mfxStatus ConvertFrameRate(mfxF64 dFrameRate, mfxU32* pnFrameRateExtN, mfxU32* pnFrameRateExtD);
mfxU16 GetFreeSurface(mfxFrameSurface1* pSurfacesPool, mfxU16 nPoolSize);

class CSmplYUVReader
{
public:
	typedef std::list<std::string>::iterator ls_iterator;
	CSmplYUVReader();
	virtual ~CSmplYUVReader();

	virtual void Close();
	virtual mfxStatus Init(std::list<std::string> inputs, mfxU32 ColorFormat, bool shouldShiftP010 = false);
	virtual mfxStatus LoadNextFrame(mfxFrameSurface1* pSurface);
	virtual void Reset();
	mfxU32 m_ColorFormat; // color format of input YUV data, YUV420 or NV12

protected:

	std::vector<FILE*> m_files;

	bool shouldShiftP010High;
	bool m_bInited;
};

class CSmplBitstreamWriter
{
public:

	CSmplBitstreamWriter();
	virtual ~CSmplBitstreamWriter();

	virtual mfxStatus Init(const std::string& strFileName);
	virtual mfxStatus WriteNextFrame(mfxBitstream *pMfxBitstream);
	virtual mfxStatus Reset();
	virtual void Close();
	mfxU32 m_nProcessedFramesNum;

protected:
	FILE*       m_fSource;
	bool        m_bInited;
	std::string m_sFile;
};