#pragma once

#include <list>
#include <vector>

#include "mfxcommon.h"
#include "mfxmvc.h"
#include "mfxvideo.h"
#include "mfxvideo++.h"

#include "base_allocator.h"
#include "utils.h"

struct sTask
{
	mfxBitstream mfxBS;
	mfxSyncPoint EncSyncP;
	CSmplBitstreamWriter *pWriter;

	sTask();
	mfxStatus WriteBitstream();
	mfxStatus Reset();
	mfxStatus Init(mfxU32 nBufferSize, CSmplBitstreamWriter *pWriter = NULL);
	mfxStatus Close();
};

class CEncTaskPool
{
public:
	CEncTaskPool();
	virtual ~CEncTaskPool();

	virtual mfxStatus Init(MFXVideoSession* pmfxSession, CSmplBitstreamWriter* pWriter, mfxU32 nPoolSize, mfxU32 nBufferSize);
	virtual mfxStatus GetFreeTask(sTask **ppTask);
	virtual mfxStatus SynchronizeFirstTask();

	virtual void Close();
	virtual void ClearTasks();
protected:
	sTask* m_pTasks;
	mfxU32 m_nPoolSize;
	mfxU32 m_nTaskBufferStart;

	MFXVideoSession* m_pmfxSession;

	virtual mfxU32 GetFreeTaskIndex();
};

struct sInputParams
{
	mfxU16 nWidth; // source picture width
	mfxU16 nHeight; // source picture height
	mfxF64 dFrameRate;
	mfxU16 nBitRate;
	mfxU32 FileInputFourCC;
	std::list<std::string> InputFiles;
	std::string dstFileBuff;
};

class CEncodingPipeline
{
public:
	CEncodingPipeline();
	virtual ~CEncodingPipeline();

	mfxStatus Init(sInputParams *pParams);
	mfxStatus Run();
	void Close();
	mfxStatus ResetMFXComponents(sInputParams* pParams);

private:
	void InsertIDR(bool bIsNextFrameIDR);
	mfxStatus InitEncFrameParams(sTask* pTask);

	mfxStatus CreateAllocator();
	void DeleteAllocator();

	mfxStatus InitMfxEncParams(sInputParams *pParams);
	mfxStatus InitFileWriter(CSmplBitstreamWriter **ppWriter, const std::string& filename);
	void FreeFileWriter();

	mfxStatus AllocFrames();
	void DeleteFrames();

	virtual mfxStatus AllocateSufficientBuffer(mfxBitstream* pBS);
	mfxStatus LoadNextFrame(mfxFrameSurface1* pSurf);

	mfxStatus GetFreeTask(sTask **ppTask);
	MFXVideoSession& GetFirstSession() { return m_mfxSession; }
	MFXVideoENCODE* GetFirstEncoder() { return m_pmfxENC; }

private:
	CSmplBitstreamWriter *m_FileWriter;
	CSmplYUVReader m_FileReader;
	CEncTaskPool m_TaskPool;

	MFXVideoSession m_mfxSession;
	MFXVideoENCODE* m_pmfxENC;

	mfxVideoParam m_mfxEncParams;

	MFXFrameAllocator* m_pMFXAllocator;

	mfxFrameSurface1* m_pEncSurfaces; // frames array for encoder input (vpp output)
	mfxFrameAllocResponse m_EncResponse;  // memory allocation response for encoder

	mfxU32 m_InputFourCC;
	
	mfxU32 m_nFramesRead;
	bool   m_bInsertIDR;

	mfxEncodeCtrl m_encCtrl;
};