#include "utils.h"

#include <windows.h>
#include <iostream>

typedef mfxI64 msdk_tick;
#define MSDK_GET_TIME(T,S,F) ((mfxF64)((T)-(S))/(mfxF64)(F))

msdk_tick msdk_time_get_tick(void)
{
	LARGE_INTEGER t1;

	QueryPerformanceCounter(&t1);
	return t1.QuadPart;
}

msdk_tick msdk_time_get_frequency(void)
{
	LARGE_INTEGER t1;

	QueryPerformanceFrequency(&t1);
	return t1.QuadPart;
}

class CTimer
{
public:
	CTimer() :
		start(0)
	{
	}
	static msdk_tick GetFrequency()
	{
		if (!frequency) frequency = msdk_time_get_frequency();
		return frequency;
	}
	static mfxF64 ConvertToSeconds(msdk_tick elapsed)
	{
		return MSDK_GET_TIME(elapsed, 0, GetFrequency());
	}

	inline void Start()
	{
		start = msdk_time_get_tick();
	}
	inline msdk_tick GetDelta()
	{
		return msdk_time_get_tick() - start;
	}
	inline mfxF64 GetTime()
	{
		return MSDK_GET_TIME(msdk_time_get_tick(), start, GetFrequency());
	}

protected:
	static msdk_tick frequency;
	msdk_tick start;
private:
	CTimer(const CTimer&);
	void operator=(const CTimer&);
};

msdk_tick CTimer::frequency = 0;

void WipeMfxBitstream(mfxBitstream* pBitstream)
{
	MSDK_CHECK_POINTER(pBitstream);

	//free allocated memory
	MSDK_SAFE_DELETE_ARRAY(pBitstream->Data);
}

mfxStatus InitMfxBitstream(mfxBitstream* pBitstream, mfxU32 nSize)
{
	//check input params
	MSDK_CHECK_POINTER(pBitstream, MFX_ERR_NULL_PTR);
	MSDK_CHECK_ERROR(nSize, 0, MFX_ERR_NOT_INITIALIZED);

	//prepare pBitstream
	WipeMfxBitstream(pBitstream);

	//prepare buffer
	pBitstream->Data = new mfxU8[nSize];
	MSDK_CHECK_POINTER(pBitstream->Data, MFX_ERR_MEMORY_ALLOC);

	pBitstream->MaxLength = nSize;

	return MFX_ERR_NONE;
}

mfxStatus ExtendMfxBitstream(mfxBitstream* pBitstream, mfxU32 nSize)
{
	MSDK_CHECK_POINTER(pBitstream, MFX_ERR_NULL_PTR);

	MSDK_CHECK_ERROR(nSize <= pBitstream->MaxLength, true, MFX_ERR_UNSUPPORTED);

	mfxU8* pData = new mfxU8[nSize];
	MSDK_CHECK_POINTER(pData, MFX_ERR_MEMORY_ALLOC);

	memmove(pData, pBitstream->Data + pBitstream->DataOffset, pBitstream->DataLength);

	WipeMfxBitstream(pBitstream);

	pBitstream->Data = pData;
	pBitstream->DataOffset = 0;
	pBitstream->MaxLength = nSize;

	return MFX_ERR_NONE;
}

CSmplBitstreamWriter::CSmplBitstreamWriter()
{
	m_fSource = NULL;
	m_bInited = false;
	m_nProcessedFramesNum = 0;
}

CSmplBitstreamWriter::~CSmplBitstreamWriter()
{
	Close();
}

void CSmplBitstreamWriter::Close()
{
	if (m_fSource)
	{
		fclose(m_fSource);
		m_fSource = NULL;
	}

	m_bInited = false;
}

mfxStatus CSmplBitstreamWriter::Init(const std::string& strFileName)
{
	if (strFileName.empty()) {
		return MFX_ERR_NONE;
	}

	Close();

	//init file to write encoded data
	m_fSource = fopen(strFileName.c_str(), "wb+");
	MSDK_CHECK_POINTER(m_fSource, MFX_ERR_NULL_PTR);

	m_sFile = strFileName;
	//set init state to true in case of success
	m_bInited = true;
	return MFX_ERR_NONE;
}

mfxStatus CSmplBitstreamWriter::Reset()
{
	return Init(m_sFile.c_str());
}

mfxStatus CSmplBitstreamWriter::WriteNextFrame(mfxBitstream *pMfxBitstream)
{
	// check if writer is initialized
	MSDK_CHECK_ERROR(m_bInited, false, MFX_ERR_NOT_INITIALIZED);
	MSDK_CHECK_POINTER(pMfxBitstream, MFX_ERR_NULL_PTR);

	mfxU32 nBytesWritten = 0;

	nBytesWritten = (mfxU32)fwrite(pMfxBitstream->Data + pMfxBitstream->DataOffset, 1, pMfxBitstream->DataLength, m_fSource);
	if (nBytesWritten != pMfxBitstream->DataLength) {
		return MFX_ERR_UNDEFINED_BEHAVIOR;
	}

	// mark that we don't need bit stream data any more
	pMfxBitstream->DataLength = 0;

	m_nProcessedFramesNum++;

	// print encoding progress to console every certain number of frames (not to affect performance too much)
	if (1 == m_nProcessedFramesNum || (0 == (m_nProcessedFramesNum % 100)))
	{
		std::cout << "Frame number: " << m_nProcessedFramesNum << std::endl;
	}

	return MFX_ERR_NONE;
}

CSmplYUVReader::CSmplYUVReader()
{
	m_bInited = false;
	m_ColorFormat = MFX_FOURCC_YV12;
	shouldShiftP010High = false;
}

mfxStatus CSmplYUVReader::Init(std::list<std::string> inputs, mfxU32 ColorFormat, bool shouldShiftP010)
{
	Close();

	if (MFX_FOURCC_NV12 != ColorFormat &&
		MFX_FOURCC_YV12 != ColorFormat &&
		MFX_FOURCC_I420 != ColorFormat &&
		MFX_FOURCC_YUY2 != ColorFormat &&
		MFX_FOURCC_RGB4 != ColorFormat &&
		MFX_FOURCC_BGR4 != ColorFormat &&
		MFX_FOURCC_P010 != ColorFormat &&
		MFX_FOURCC_P210 != ColorFormat)
	{
		return MFX_ERR_UNSUPPORTED;
	}

	if (MFX_FOURCC_P010 == ColorFormat)
	{
		shouldShiftP010High = shouldShiftP010;
	}

	if (!inputs.size())
	{
		return MFX_ERR_UNSUPPORTED;
	}

	for (ls_iterator it = inputs.begin(); it != inputs.end(); it++)
	{
		FILE *f = fopen((*it).c_str(), "rb");
		MSDK_CHECK_POINTER(f, MFX_ERR_NULL_PTR);

		m_files.push_back(f);
	}

	m_ColorFormat = ColorFormat;

	m_bInited = true;

	return MFX_ERR_NONE;
}

CSmplYUVReader::~CSmplYUVReader()
{
	Close();
}

void CSmplYUVReader::Close()
{
	for (mfxU32 i = 0; i < m_files.size(); i++)
	{
		fclose(m_files[i]);
	}
	m_files.clear();
	m_bInited = false;
}

void CSmplYUVReader::Reset()
{
	for (mfxU32 i = 0; i < m_files.size(); i++)
	{
		fseek(m_files[i], 0, SEEK_SET);
	}
}

mfxStatus CSmplYUVReader::LoadNextFrame(mfxFrameSurface1* pSurface)
{
	// check if reader is initialized
	MSDK_CHECK_ERROR(m_bInited, false, MFX_ERR_NOT_INITIALIZED);
	MSDK_CHECK_POINTER(pSurface, MFX_ERR_NULL_PTR);

	mfxU32 nBytesRead;
	mfxU16 w, h, i, pitch;
	mfxU8 *ptr, *ptr2;
	mfxFrameInfo& pInfo = pSurface->Info;
	mfxFrameData& pData = pSurface->Data;

	mfxU32 vid = pInfo.FrameId.ViewId;

	if (vid > m_files.size())
	{
		return MFX_ERR_UNSUPPORTED;
	}

	if (pInfo.CropH > 0 && pInfo.CropW > 0)
	{
		w = pInfo.CropW;
		h = pInfo.CropH;
	}
	else
	{
		w = pInfo.Width;
		h = pInfo.Height;
	}

	mfxU32 nBytesPerPixel = (pInfo.FourCC == MFX_FOURCC_P010 || pInfo.FourCC == MFX_FOURCC_P210) ? 2 : 1;

	if (MFX_FOURCC_YUY2 == pInfo.FourCC || MFX_FOURCC_RGB4 == pInfo.FourCC || MFX_FOURCC_BGR4 == pInfo.FourCC)
	{
		//Packed format: Luminance and chrominance are on the same plane
		switch (m_ColorFormat)
		{
		case MFX_FOURCC_RGB4:
		case MFX_FOURCC_BGR4:

			pitch = pData.Pitch;
			ptr = MSDK_MIN(MSDK_MIN(pData.R, pData.G), pData.B);
			ptr = ptr + pInfo.CropX + pInfo.CropY * pData.Pitch;

			for (i = 0; i < h; i++)
			{
				nBytesRead = (mfxU32)fread(ptr + i * pitch, 1, 4 * w, m_files[vid]);

				if ((mfxU32)4 * w != nBytesRead)
				{
					return MFX_ERR_MORE_DATA;
				}
			}
			break;
		case MFX_FOURCC_YUY2:
			pitch = pData.Pitch;
			ptr = pData.Y + pInfo.CropX + pInfo.CropY * pData.Pitch;

			for (i = 0; i < h; i++)
			{
				nBytesRead = (mfxU32)fread(ptr + i * pitch, 1, 2 * w, m_files[vid]);

				if ((mfxU32)2 * w != nBytesRead)
				{
					return MFX_ERR_MORE_DATA;
				}
			}
			break;
		default:
			return MFX_ERR_UNSUPPORTED;
		}
	}
	else if (MFX_FOURCC_NV12 == pInfo.FourCC || MFX_FOURCC_YV12 == pInfo.FourCC || MFX_FOURCC_P010 == pInfo.FourCC || MFX_FOURCC_P210 == pInfo.FourCC)
	{
		pitch = pData.Pitch;
		ptr = pData.Y + pInfo.CropX + pInfo.CropY * pData.Pitch;

		// read luminance plane
		for (i = 0; i < h; i++)
		{
			nBytesRead = (mfxU32)fread(ptr + i * pitch, nBytesPerPixel, w, m_files[vid]);

			if (w != nBytesRead)
			{
				return MFX_ERR_MORE_DATA;
			}

			// Shifting data if required
			if ((MFX_FOURCC_P010 == pInfo.FourCC || MFX_FOURCC_P210 == pInfo.FourCC) && shouldShiftP010High)
			{
				mfxU16* shortPtr = (mfxU16*)(ptr + i * pitch);
				for (int idx = 0; idx < w; idx++)
				{
					shortPtr[idx] <<= 6;
				}
			}
		}

		// read chroma planes
		switch (m_ColorFormat) // color format of data in the input file
		{
		case MFX_FOURCC_I420:
		case MFX_FOURCC_YV12:
			switch (pInfo.FourCC)
			{
			case MFX_FOURCC_NV12:

				mfxU8 buf[2048]; // maximum supported chroma width for nv12
				mfxU32 j, dstOffset[2];
				w /= 2;
				h /= 2;
				ptr = pData.UV + pInfo.CropX + (pInfo.CropY / 2) * pitch;
				if (w > 2048)
				{
					return MFX_ERR_UNSUPPORTED;
				}

				if (m_ColorFormat == MFX_FOURCC_I420) {
					dstOffset[0] = 0;
					dstOffset[1] = 1;
				}
				else {
					dstOffset[0] = 1;
					dstOffset[1] = 0;
				}

				// load first chroma plane: U (input == I420) or V (input == YV12)
				for (i = 0; i < h; i++)
				{
					nBytesRead = (mfxU32)fread(buf, 1, w, m_files[vid]);
					if (w != nBytesRead)
					{
						return MFX_ERR_MORE_DATA;
					}
					for (j = 0; j < w; j++)
					{
						ptr[i * pitch + j * 2 + dstOffset[0]] = buf[j];
					}
				}

				// load second chroma plane: V (input == I420) or U (input == YV12)
				for (i = 0; i < h; i++)
				{

					nBytesRead = (mfxU32)fread(buf, 1, w, m_files[vid]);

					if (w != nBytesRead)
					{
						return MFX_ERR_MORE_DATA;
					}
					for (j = 0; j < w; j++)
					{
						ptr[i * pitch + j * 2 + dstOffset[1]] = buf[j];
					}
				}

				break;
			case MFX_FOURCC_YV12:
				w /= 2;
				h /= 2;
				pitch /= 2;

				if (m_ColorFormat == MFX_FOURCC_I420) {
					ptr = pData.U + (pInfo.CropX / 2) + (pInfo.CropY / 2) * pitch;
					ptr2 = pData.V + (pInfo.CropX / 2) + (pInfo.CropY / 2) * pitch;
				}
				else {
					ptr = pData.V + (pInfo.CropX / 2) + (pInfo.CropY / 2) * pitch;
					ptr2 = pData.U + (pInfo.CropX / 2) + (pInfo.CropY / 2) * pitch;
				}

				for (i = 0; i < h; i++)
				{

					nBytesRead = (mfxU32)fread(ptr + i * pitch, 1, w, m_files[vid]);

					if (w != nBytesRead)
					{
						return MFX_ERR_MORE_DATA;
					}
				}
				for (i = 0; i < h; i++)
				{
					nBytesRead = (mfxU32)fread(ptr2 + i * pitch, 1, w, m_files[vid]);

					if (w != nBytesRead)
					{
						return MFX_ERR_MORE_DATA;
					}
				}
				break;
			default:
				return MFX_ERR_UNSUPPORTED;
			}
			break;
		case MFX_FOURCC_NV12:
		case MFX_FOURCC_P010:
		case MFX_FOURCC_P210:
			if (MFX_FOURCC_P210 != pInfo.FourCC)
			{
				h /= 2;
			}
			ptr = pData.UV + pInfo.CropX + (pInfo.CropY / 2) * pitch;
			for (i = 0; i < h; i++)
			{
				nBytesRead = (mfxU32)fread(ptr + i * pitch, nBytesPerPixel, w, m_files[vid]);

				if (w != nBytesRead)
				{
					return MFX_ERR_MORE_DATA;
				}

				// Shifting data if required
				if ((MFX_FOURCC_P010 == pInfo.FourCC || MFX_FOURCC_P210 == pInfo.FourCC) && shouldShiftP010High)
				{
					mfxU16* shortPtr = (mfxU16*)(ptr + i * pitch);
					for (int idx = 0; idx < w; idx++)
					{
						shortPtr[idx] <<= 6;
					}
				}
			}

			break;
		default:
			return MFX_ERR_UNSUPPORTED;
		}
	}

	return MFX_ERR_NONE;
}

mfxStatus ConvertFrameRate(mfxF64 dFrameRate, mfxU32* pnFrameRateExtN, mfxU32* pnFrameRateExtD)
{
	MSDK_CHECK_POINTER(pnFrameRateExtN, MFX_ERR_NULL_PTR);
	MSDK_CHECK_POINTER(pnFrameRateExtD, MFX_ERR_NULL_PTR);

	mfxU32 fr;

	fr = (mfxU32)(dFrameRate + .5);

	if (fabs(fr - dFrameRate) < 0.0001)
	{
		*pnFrameRateExtN = fr;
		*pnFrameRateExtD = 1;
		return MFX_ERR_NONE;
	}

	fr = (mfxU32)(dFrameRate * 1.001 + .5);

	if (fabs(fr * 1000 - dFrameRate * 1001) < 10)
	{
		*pnFrameRateExtN = fr * 1000;
		*pnFrameRateExtD = 1001;
		return MFX_ERR_NONE;
	}

	*pnFrameRateExtN = (mfxU32)(dFrameRate * 10000 + .5);
	*pnFrameRateExtD = 10000;

	return MFX_ERR_NONE;
}

mfxU16 GetFreeSurfaceIndex(mfxFrameSurface1* pSurfacesPool, mfxU16 nPoolSize)
{
	if (pSurfacesPool)
	{
		for (mfxU16 i = 0; i < nPoolSize; i++)
		{
			if (0 == pSurfacesPool[i].Data.Locked)
			{
				return i;
			}
		}
	}

	return MSDK_INVALID_SURF_IDX;
}

mfxU16 GetFreeSurface(mfxFrameSurface1* pSurfacesPool, mfxU16 nPoolSize)
{
	mfxU32 SleepInterval = 10; // milliseconds

	mfxU16 idx = MSDK_INVALID_SURF_IDX;

	CTimer t;
	t.Start();
	//wait if there's no free surface
	do
	{
		idx = GetFreeSurfaceIndex(pSurfacesPool, nPoolSize);

		if (MSDK_INVALID_SURF_IDX != idx)
		{
			break;
		}
		else
		{
			MSDK_SLEEP(SleepInterval);
		}
	} while (t.GetTime() < MSDK_SURFACE_WAIT_INTERVAL / 1000);

	if (idx == MSDK_INVALID_SURF_IDX)
	{
		std::cerr << "ERROR: No free surfaces in pool (during long period)" << std::endl;
	}

	return idx;
}