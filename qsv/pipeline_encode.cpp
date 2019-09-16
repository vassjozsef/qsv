#include "pipeline_encode.h"

#include <cstring>
#include <iostream>
#include <windows.h>

#include "sysmem_allocator.h"

CEncTaskPool::CEncTaskPool()
{
	m_pTasks = NULL;
	m_pmfxSession = NULL;
	m_nTaskBufferStart = 0;
	m_nPoolSize = 0;
	m_bGpuHangRecovery = false;
}

CEncTaskPool::~CEncTaskPool()
{
	Close();
}

mfxStatus CEncTaskPool::Init(MFXVideoSession* pmfxSession, CSmplBitstreamWriter* pWriter, mfxU32 nPoolSize, mfxU32 nBufferSize)
{
	MSDK_CHECK_POINTER(pmfxSession, MFX_ERR_NULL_PTR);

	MSDK_CHECK_ERROR(nPoolSize, 0, MFX_ERR_UNDEFINED_BEHAVIOR);
	MSDK_CHECK_ERROR(nBufferSize, 0, MFX_ERR_UNDEFINED_BEHAVIOR);

	m_pmfxSession = pmfxSession;
	m_nPoolSize = nPoolSize;

	m_pTasks = new sTask[m_nPoolSize];
	if (!m_pTasks) {
		return MFX_ERR_MEMORY_ALLOC;
	}

	mfxStatus sts = MFX_ERR_NONE;

	for (mfxU32 i = 0; i < m_nPoolSize; i++)
	{
		sts = m_pTasks[i].Init(nBufferSize, pWriter);
		MSDK_CHECK_STATUS(sts, "m_pTasks[i].Init failed");
	}

	return MFX_ERR_NONE;
}

mfxStatus CEncTaskPool::SynchronizeFirstTask()
{
	MSDK_CHECK_POINTER(m_pTasks, MFX_ERR_NOT_INITIALIZED);
	MSDK_CHECK_POINTER(m_pmfxSession, MFX_ERR_NOT_INITIALIZED);

	mfxStatus sts = MFX_ERR_NONE;
	bool bGpuHang = false;

	// non-null sync point indicates that task is in execution
	if (NULL != m_pTasks[m_nTaskBufferStart].EncSyncP)
	{
		sts = m_pmfxSession->SyncOperation(m_pTasks[m_nTaskBufferStart].EncSyncP, MSDK_WAIT_INTERVAL);
		if (sts == MFX_ERR_GPU_HANG && m_bGpuHangRecovery)
		{
			bGpuHang = true;
			{
				for (mfxU32 i = 0; i < m_nPoolSize; i++)
					if (m_pTasks[i].EncSyncP != NULL)
					{
						sts = m_pmfxSession->SyncOperation(m_pTasks[i].EncSyncP, 0);//MSDK_WAIT_INTERVAL
					}
			}
			ClearTasks();
			sts = MFX_ERR_NONE;
			std::cout << "GPU hang happened" << std::endl;
		}
		MSDK_CHECK_STATUS_NO_RET(sts, "SyncOperation failed");

		if (MFX_ERR_NONE == sts)
		{
			sts = m_pTasks[m_nTaskBufferStart].WriteBitstream();
			MSDK_CHECK_STATUS(sts, "m_pTasks[m_nTaskBufferStart].WriteBitstream failed");

			sts = m_pTasks[m_nTaskBufferStart].Reset();
			MSDK_CHECK_STATUS(sts, "m_pTasks[m_nTaskBufferStart].Reset failed");

			// move task buffer start to the next executing task
			// the first transform frame to the right with non zero sync point
			for (mfxU32 i = 0; i < m_nPoolSize; i++)
			{
				m_nTaskBufferStart = (m_nTaskBufferStart + 1) % m_nPoolSize;
				if (NULL != m_pTasks[m_nTaskBufferStart].EncSyncP)
				{
					break;
				}
			}
		}
	}
	else
	{
		sts = MFX_ERR_NOT_FOUND; // no tasks left in task buffer
	}
	return bGpuHang ? MFX_ERR_GPU_HANG : sts;
}

mfxU32 CEncTaskPool::GetFreeTaskIndex()
{
	mfxU32 off = 0;

	if (m_pTasks)
	{
		for (off = 0; off < m_nPoolSize; off++)
		{
			if (NULL == m_pTasks[(m_nTaskBufferStart + off) % m_nPoolSize].EncSyncP)
			{
				break;
			}
		}
	}

	if (off >= m_nPoolSize)
		return m_nPoolSize;

	return (m_nTaskBufferStart + off) % m_nPoolSize;
}

mfxStatus CEncTaskPool::GetFreeTask(sTask **ppTask)
{
	MSDK_CHECK_POINTER(ppTask, MFX_ERR_NULL_PTR);
	MSDK_CHECK_POINTER(m_pTasks, MFX_ERR_NOT_INITIALIZED);

	mfxU32 index = GetFreeTaskIndex();

	if (index >= m_nPoolSize)
	{
		return MFX_ERR_NOT_FOUND;
	}

	// return the address of the task
	*ppTask = &m_pTasks[index];

	return MFX_ERR_NONE;
}

void CEncTaskPool::Close()
{
	if (m_pTasks)
	{
		for (mfxU32 i = 0; i < m_nPoolSize; i++)
		{
			m_pTasks[i].Close();
		}
	}

	MSDK_SAFE_DELETE_ARRAY(m_pTasks);

	m_pmfxSession = NULL;
	m_nTaskBufferStart = 0;
	m_nPoolSize = 0;
}

void CEncTaskPool::SetGpuHangRecoveryFlag()
{
	m_bGpuHangRecovery = true;
}

void CEncTaskPool::ClearTasks()
{
	for (size_t i = 0; i < m_nPoolSize; i++)
	{
		m_pTasks[i].Reset();
	}
	m_nTaskBufferStart = 0;
}

sTask::sTask()
	: EncSyncP(0)
	, pWriter(NULL)
{
	MSDK_ZERO_MEMORY(mfxBS);
}

mfxStatus sTask::Init(mfxU32 nBufferSize, CSmplBitstreamWriter *pwriter)
{
	Close();

	pWriter = pwriter;

	mfxStatus sts = Reset();
	MSDK_CHECK_STATUS(sts, "Reset failed");

	sts = InitMfxBitstream(&mfxBS, nBufferSize);
	MSDK_CHECK_STATUS_SAFE(sts, "InitMfxBitstream failed", WipeMfxBitstream(&mfxBS));

	return sts;
}

mfxStatus sTask::Close()
{
	WipeMfxBitstream(&mfxBS);
	EncSyncP = 0;

	return MFX_ERR_NONE;
}

mfxStatus sTask::WriteBitstream()
{
	if (pWriter)
		return pWriter->WriteNextFrame(&mfxBS);
	else
		return MFX_ERR_NONE;
}

mfxStatus sTask::Reset()
{
	// mark sync point as free
	EncSyncP = NULL;

	// prepare bit stream
	mfxBS.DataOffset = 0;
	mfxBS.DataLength = 0;

	return MFX_ERR_NONE;
}

CEncodingPipeline::CEncodingPipeline()
{
	m_pmfxENC = NULL;
	m_pMFXAllocator = NULL;
	m_pmfxAllocatorParams = NULL;
	m_pEncSurfaces = NULL;
	m_InputFourCC = 0;

	m_nFramesRead = 0;
	m_bFileWriterReset = false;

	MSDK_ZERO_MEMORY(m_mfxEncParams);

	MSDK_ZERO_MEMORY(m_EncResponse);

	m_bCutOutput = false;
	m_bInsertIDR = false;

	MSDK_ZERO_MEMORY(m_encCtrl);
}

CEncodingPipeline::~CEncodingPipeline()
{
	Close();
}

mfxStatus CEncodingPipeline::InitFileWriter(CSmplBitstreamWriter **ppWriter, const std::string& filename)
{
	MSDK_CHECK_ERROR(ppWriter, NULL, MFX_ERR_NULL_PTR);

	MSDK_SAFE_DELETE(*ppWriter);
	*ppWriter = new CSmplBitstreamWriter;
	MSDK_CHECK_POINTER(*ppWriter, MFX_ERR_MEMORY_ALLOC);
	mfxStatus sts = (*ppWriter)->Init(filename);
	MSDK_CHECK_STATUS(sts, " failed");

	return sts;
}

void CEncodingPipeline::FreeFileWriter()
{
	if (m_FileWriter) {
		m_FileWriter->Close();
	}
	MSDK_SAFE_DELETE(m_FileWriter);
}

mfxStatus CEncodingPipeline::Init(sInputParams *pParams) {
	MSDK_CHECK_POINTER(pParams, MFX_ERR_NULL_PTR);

	mfxStatus sts = MFX_ERR_NONE;

	m_InputFourCC = MFX_FOURCC_NV12;

	mfxInitParam initPar;
	mfxVersion version;     // real API version with which library is initialized

	MSDK_ZERO_MEMORY(initPar);

	// we set version to 1.0 and later we will query actual version of the library which will got leaded
	initPar.Version.Major = 1;
	initPar.Version.Minor = 0;

	initPar.GPUCopy = 0;

	// Init session - hw only
	// try searching on all display adapters
	initPar.Implementation = MFX_IMPL_HARDWARE_ANY;

	// Library should pick first available compatible adapter during InitEx call with MFX_IMPL_HARDWARE_ANY
	sts = m_mfxSession.InitEx(initPar);

	MSDK_CHECK_STATUS(sts, "m_mfxSession.InitEx failed");

	sts = MFXQueryVersion(m_mfxSession, &version); // get real API version of the loaded library
	MSDK_CHECK_STATUS(sts, "MFXQueryVersion failed");

	// create encoder
	m_pmfxENC = new MFXVideoENCODE(m_mfxSession);
	MSDK_CHECK_POINTER(m_pmfxENC, MFX_ERR_MEMORY_ALLOC);

	// prepare input file reader
	sts = m_FileReader.Init(pParams->InputFiles, pParams->FileInputFourCC);
	MSDK_CHECK_STATUS(sts, "m_FileReader.Init failed");

	sts = InitFileWriter(&m_FileWriter, pParams->dstFileBuff);
	MSDK_CHECK_STATUS(sts, "InitFileWriter failed");

	// create and init frame allocator
	sts = CreateAllocator();
	MSDK_CHECK_STATUS(sts, "CreateAllocator failed");

	sts = InitMfxEncParams(pParams);
	MSDK_CHECK_STATUS(sts, "InitMfxEncParams failed");

	sts = ResetMFXComponents(pParams);
	MSDK_CHECK_STATUS(sts, "ResetMFXComponents failed");

	return MFX_ERR_NONE;
}

void CEncodingPipeline::Close()
{
	if (m_FileWriter) {
		std::cout << "Frame number : %u" << m_FileWriter->m_nProcessedFramesNum << std::endl;
	}

	MSDK_SAFE_DELETE(m_pmfxENC);

	DeleteFrames();

	m_TaskPool.Close();
	m_mfxSession.Close();

	m_FileReader.Close();
	FreeFileWriter();

	// allocator if used as external for MediaSDK must be deleted after SDK components
	DeleteAllocator();
}

mfxStatus CEncodingPipeline::CreateAllocator()
{
	mfxStatus sts = MFX_ERR_NONE;

	// create system memory allocator
	m_pMFXAllocator = new SysMemFrameAllocator;
	MSDK_CHECK_POINTER(m_pMFXAllocator, MFX_ERR_MEMORY_ALLOC);

	// initialize memory allocator
	sts = m_pMFXAllocator->Init(m_pmfxAllocatorParams);
	MSDK_CHECK_STATUS(sts, "m_pMFXAllocator->Init failed");

	return MFX_ERR_NONE;
}

void CEncodingPipeline::DeleteAllocator()
{
	// delete allocator
	MSDK_SAFE_DELETE(m_pMFXAllocator);
	MSDK_SAFE_DELETE(m_pmfxAllocatorParams);

//	DeleteHWDevice();
}

mfxStatus CEncodingPipeline::InitMfxEncParams(sInputParams *pInParams)
{
	m_mfxEncParams.mfx.CodecId = MFX_CODEC_AVC;
	m_mfxEncParams.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED; // trade-off between quality and speed
	m_mfxEncParams.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
	m_mfxEncParams.mfx.GopRefDist = 0;
	m_mfxEncParams.mfx.GopPicSize = 0;
	m_mfxEncParams.mfx.NumRefFrame = 0;
	m_mfxEncParams.mfx.IdrInterval = 0;

	m_mfxEncParams.mfx.CodecProfile = 0;
	m_mfxEncParams.mfx.CodecLevel = 0;
	m_mfxEncParams.mfx.MaxKbps = 0;
	m_mfxEncParams.mfx.InitialDelayInKB = 0;
	m_mfxEncParams.mfx.GopOptFlag = 0;
	m_mfxEncParams.mfx.BufferSizeInKB = 0;

	if (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_CQP)
	{
		m_mfxEncParams.mfx.QPI = 0;
		m_mfxEncParams.mfx.QPP = 0;
		m_mfxEncParams.mfx.QPB = 0;
	}
	else
	{
		m_mfxEncParams.mfx.TargetKbps = pInParams->nBitRate; // in Kbps
	}

	m_mfxEncParams.mfx.LowPower = MFX_CODINGOPTION_ON;

	m_mfxEncParams.mfx.NumSlice = 0;
	ConvertFrameRate(pInParams->dFrameRate, &m_mfxEncParams.mfx.FrameInfo.FrameRateExtN, &m_mfxEncParams.mfx.FrameInfo.FrameRateExtD);
	m_mfxEncParams.mfx.EncodedOrder = 0; // binary flag, 0 signals encoder to take frames in display order

	// specify memory type
	m_mfxEncParams.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;

	// frame info parameters
	m_mfxEncParams.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
	m_mfxEncParams.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
	m_mfxEncParams.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
	m_mfxEncParams.mfx.FrameInfo.Shift = 0;

	// width must be a multiple of 16
	// height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture
	m_mfxEncParams.mfx.FrameInfo.Width = MSDK_ALIGN16(pInParams->nWidth);
	m_mfxEncParams.mfx.FrameInfo.Height = (MFX_PICSTRUCT_PROGRESSIVE == m_mfxEncParams.mfx.FrameInfo.PicStruct) ?
		MSDK_ALIGN16(pInParams->nHeight) : MSDK_ALIGN32(pInParams->nHeight);

	m_mfxEncParams.mfx.FrameInfo.CropX = 0;
	m_mfxEncParams.mfx.FrameInfo.CropY = 0;
	m_mfxEncParams.mfx.FrameInfo.CropW = pInParams->nWidth;
	m_mfxEncParams.mfx.FrameInfo.CropH = pInParams->nHeight;

	m_mfxEncParams.AsyncDepth = 4;

	return MFX_ERR_NONE;
}

mfxStatus CEncodingPipeline::ResetMFXComponents(sInputParams* pParams)
{
	MSDK_CHECK_POINTER(pParams, MFX_ERR_NULL_PTR);
	MSDK_CHECK_POINTER(m_pmfxENC, MFX_ERR_NOT_INITIALIZED);

	mfxStatus sts = MFX_ERR_NONE;

	sts = m_pmfxENC->Close();
	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_NOT_INITIALIZED);
	MSDK_CHECK_STATUS(sts, "m_pmfxENC->Close failed");

	// free allocated frames
	DeleteFrames();

	m_TaskPool.Close();

	sts = AllocFrames();
	MSDK_CHECK_STATUS(sts, "AllocFrames failed");

	sts = m_pmfxENC->Init(&m_mfxEncParams);
	if (MFX_WRN_PARTIAL_ACCELERATION == sts)
	{
		std::cout << "WARNING: partial acceleration" << std::endl;
		MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
	}

	MSDK_CHECK_STATUS(sts, "m_pmfxENC->Init failed");

	mfxU32 nEncodedDataBufferSize = m_mfxEncParams.mfx.FrameInfo.Width * m_mfxEncParams.mfx.FrameInfo.Height * 4;
	sts = m_TaskPool.Init(&m_mfxSession, m_FileWriter, m_mfxEncParams.AsyncDepth, nEncodedDataBufferSize);
	MSDK_CHECK_STATUS(sts, "m_TaskPool.Init failed");

	return MFX_ERR_NONE;
}

mfxStatus CEncodingPipeline::AllocFrames()
{
	MSDK_CHECK_POINTER(GetFirstEncoder(), MFX_ERR_NOT_INITIALIZED);

	mfxStatus sts = MFX_ERR_NONE;
	mfxFrameAllocRequest EncRequest;
	mfxFrameAllocRequest VppRequest[2];

	mfxU16 nEncSurfNum = 0; // number of surfaces for encoder
	mfxU16 nVppSurfNum = 0; // number of surfaces for vpp

	MSDK_ZERO_MEMORY(EncRequest);
	MSDK_ZERO_MEMORY(VppRequest[0]);
	MSDK_ZERO_MEMORY(VppRequest[1]);

	// Querying encoder
	sts = GetFirstEncoder()->Query(&m_mfxEncParams, &m_mfxEncParams);
	MSDK_CHECK_STATUS(sts, "Query (for encoder) failed");

	// Calculate the number of surfaces for components.
	// QueryIOSurf functions tell how many surfaces are required to produce at least 1 output.
	// To achieve better performance we provide extra surfaces.
	// 1 extra surface at input allows to get 1 extra output.
	sts = GetFirstEncoder()->QueryIOSurf(&m_mfxEncParams, &EncRequest);
	MSDK_CHECK_STATUS(sts, "QueryIOSurf (for encoder) failed");

	EncRequest.NumFrameMin = EncRequest.NumFrameSuggested = MSDK_MAX(EncRequest.NumFrameSuggested, 0);

	if (EncRequest.NumFrameSuggested < m_mfxEncParams.AsyncDepth)
		return MFX_ERR_MEMORY_ALLOC;

	// The number of surfaces shared by vpp output and encode input.
	nEncSurfNum = EncRequest.NumFrameSuggested;

	// prepare allocation requests
	EncRequest.NumFrameSuggested = EncRequest.NumFrameMin = nEncSurfNum;
	MSDK_MEMCPY_VAR(EncRequest.Info, &(m_mfxEncParams.mfx.FrameInfo), sizeof(mfxFrameInfo));

	// alloc frames for encoder
	sts = m_pMFXAllocator->Alloc(m_pMFXAllocator->pthis, &EncRequest, &m_EncResponse);
	MSDK_CHECK_STATUS(sts, "m_pMFXAllocator->Alloc failed");

	// prepare mfxFrameSurface1 array for encoder
	m_pEncSurfaces = new mfxFrameSurface1[m_EncResponse.NumFrameActual];
	MSDK_CHECK_POINTER(m_pEncSurfaces, MFX_ERR_MEMORY_ALLOC);

	for (int i = 0; i < m_EncResponse.NumFrameActual; i++)
	{
		memset(&(m_pEncSurfaces[i]), 0, sizeof(mfxFrameSurface1));
		MSDK_MEMCPY_VAR(m_pEncSurfaces[i].Info, &(m_mfxEncParams.mfx.FrameInfo), sizeof(mfxFrameInfo));

		// get YUV pointers
		sts = m_pMFXAllocator->Lock(m_pMFXAllocator->pthis, m_EncResponse.mids[i], &(m_pEncSurfaces[i].Data));
		MSDK_CHECK_STATUS(sts, "m_pMFXAllocator->Lock failed");
	}

	return MFX_ERR_NONE;
}

void CEncodingPipeline::DeleteFrames()
{
	// delete surfaces array
	MSDK_SAFE_DELETE_ARRAY(m_pEncSurfaces);

	// delete frames
	if (m_pMFXAllocator)
	{
		m_pMFXAllocator->Free(m_pMFXAllocator->pthis, &m_EncResponse);
	}
}

mfxStatus CEncodingPipeline::Run()
{
	MSDK_CHECK_POINTER(m_pmfxENC, MFX_ERR_NOT_INITIALIZED);

	mfxStatus sts = MFX_ERR_NONE;

	mfxFrameSurface1* pSurf = NULL; // dispatching pointer

	sTask *pCurrentTask = NULL; // a pointer to the current task
	mfxU16 nEncSurfIdx = 0;     // index of free surface for encoder input (vpp output)

									  // Since in sample we support just 2 views
									  // we will change this value between 0 and 1 in case of MVC
	mfxU16 currViewNum = 0;

	mfxU32 nFramesProcessed = 0;

	bool skipLoadingNextFrame = false;

	sts = MFX_ERR_NONE;

	// main loop, preprocessing and encoding
	while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts)
	{
		// get a pointer to a free task (bit stream and sync point for encoder)
		sts = GetFreeTask(&pCurrentTask);
		MSDK_BREAK_ON_ERROR(sts);

		// find free surface for encoder input
		
		nEncSurfIdx = GetFreeSurface(m_pEncSurfaces, m_EncResponse.NumFrameActual);
		MSDK_CHECK_ERROR(nEncSurfIdx, MSDK_INVALID_SURF_IDX, MFX_ERR_MEMORY_ALLOC);

		// point pSurf to encoder surface
		pSurf = &m_pEncSurfaces[nEncSurfIdx];
		
		if (!skipLoadingNextFrame)
		{
			pSurf->Info.FrameId.ViewId = currViewNum;

			sts = LoadNextFrame(pSurf);

			MSDK_BREAK_ON_ERROR(sts);
		}

		for (;;)
		{
			InsertIDR(m_bInsertIDR);

			sts = InitEncFrameParams(pCurrentTask);
			MSDK_CHECK_STATUS(sts, "ENCODE: InitEncFrameParams failed");

			// at this point surface for encoder contains either a frame from file or a frame processed by vpp
			sts = m_pmfxENC->EncodeFrameAsync(&m_encCtrl, &m_pEncSurfaces[nEncSurfIdx], &pCurrentTask->mfxBS, &pCurrentTask->EncSyncP);
			m_bInsertIDR = false;

			if (MFX_ERR_NONE < sts && !pCurrentTask->EncSyncP) // repeat the call if warning and no output
			{
				if (MFX_WRN_DEVICE_BUSY == sts)
					MSDK_SLEEP(1); // wait if device is busy
			}
			else if (MFX_ERR_NONE < sts && pCurrentTask->EncSyncP)
			{
				sts = MFX_ERR_NONE; // ignore warnings if output is available
				break;
			}
			else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts)
			{
				sts = AllocateSufficientBuffer(&pCurrentTask->mfxBS);
				MSDK_CHECK_STATUS(sts, "AllocateSufficientBuffer failed");
			}
			else
			{
				// get next surface and new task for 2nd bitstream in ViewOutput mode
				MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_BITSTREAM);
				break;
			}
		}

		nFramesProcessed++;
	}

	// means that the input file has ended, need to go to buffering loops
	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
	// exit in case of other errors
	MSDK_CHECK_STATUS(sts, "m_pmfxENC->EncodeFrameAsync failed");

	// loop to get buffered frames from encoder
	while (MFX_ERR_NONE <= sts)
	{
		// get a free task (bit stream and sync point for encoder)
		sts = GetFreeTask(&pCurrentTask);
		MSDK_BREAK_ON_ERROR(sts);

		for (;;)
		{
			InsertIDR(m_bInsertIDR);
			sts = m_pmfxENC->EncodeFrameAsync(&m_encCtrl, NULL, &pCurrentTask->mfxBS, &pCurrentTask->EncSyncP);
			m_bInsertIDR = false;

			if (MFX_ERR_NONE < sts && !pCurrentTask->EncSyncP) // repeat the call if warning and no output
			{
				if (MFX_WRN_DEVICE_BUSY == sts)
					MSDK_SLEEP(1); // wait if device is busy
			}
			else if (MFX_ERR_NONE < sts && pCurrentTask->EncSyncP)
			{
				sts = MFX_ERR_NONE; // ignore warnings if output is available
				break;
			}
			else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts)
			{
				sts = AllocateSufficientBuffer(&pCurrentTask->mfxBS);
				MSDK_CHECK_STATUS(sts, "AllocateSufficientBuffer failed");
			}
			else
			{
				// get new task for 2nd bitstream in ViewOutput mode
				MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_BITSTREAM);
				break;
			}
		}
		MSDK_BREAK_ON_ERROR(sts);
	}

	// loop to get buffered frames from encoder
	while (MFX_ERR_NONE <= sts)
	{
		// get a free task (bit stream and sync point for encoder)
		sts = GetFreeTask(&pCurrentTask);
		MSDK_BREAK_ON_ERROR(sts);

		for (;;)
		{
			InsertIDR(m_bInsertIDR);
			sts = m_pmfxENC->EncodeFrameAsync(&m_encCtrl, NULL, &pCurrentTask->mfxBS, &pCurrentTask->EncSyncP);
			m_bInsertIDR = false;

			if (MFX_ERR_NONE < sts && !pCurrentTask->EncSyncP) // repeat the call if warning and no output
			{
				if (MFX_WRN_DEVICE_BUSY == sts)
					MSDK_SLEEP(1); // wait if device is busy
			}
			else if (MFX_ERR_NONE < sts && pCurrentTask->EncSyncP)
			{
				sts = MFX_ERR_NONE; // ignore warnings if output is available
				break;
			}
			else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts)
			{
				sts = AllocateSufficientBuffer(&pCurrentTask->mfxBS);
				MSDK_CHECK_STATUS(sts, "AllocateSufficientBuffer failed");
			}
			else
			{
				// get new task for 2nd bitstream in ViewOutput mode
				MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_BITSTREAM);
				break;
			}
		}
		MSDK_BREAK_ON_ERROR(sts);
	}

	// MFX_ERR_MORE_DATA is the correct status to exit buffering loop with
	// indicates that there are no more buffered frames
	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
	// exit in case of other errors
	MSDK_CHECK_STATUS(sts, "m_pmfxENC->EncodeFrameAsync failed");

	// synchronize all tasks that are left in task pool
	while (MFX_ERR_NONE == sts)
	{
		sts = m_TaskPool.SynchronizeFirstTask();
	}

	// MFX_ERR_NOT_FOUND is the correct status to exit the loop with
	// EncodeFrameAsync and SyncOperation don't return this status
	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_NOT_FOUND);
	// report any errors that occurred in asynchronous part
	MSDK_CHECK_STATUS(sts, "m_TaskPool.SynchronizeFirstTask failed");
	return sts;
}

mfxStatus CEncodingPipeline::GetFreeTask(sTask **ppTask)
{
	mfxStatus sts = MFX_ERR_NONE;

	if (m_bFileWriterReset)
	{
		if (m_FileWriter)
		{
			sts = m_FileWriter->Reset();
			MSDK_CHECK_STATUS(sts, "m_FileWriters.first->Reset failed");
		}
		m_bFileWriterReset = false;
	}

	sts = m_TaskPool.GetFreeTask(ppTask);
	if (MFX_ERR_NOT_FOUND == sts)
	{
		sts = m_TaskPool.SynchronizeFirstTask();
		MSDK_CHECK_STATUS(sts, "m_TaskPool.SynchronizeFirstTask failed");

		// try again
		sts = m_TaskPool.GetFreeTask(ppTask);
	}

	return sts;
}

mfxStatus CEncodingPipeline::LoadNextFrame(mfxFrameSurface1* pSurf)
{
	mfxStatus sts = MFX_ERR_NONE;

	sts = m_FileReader.LoadNextFrame(pSurf);

	// frameorder required for reflist, dbp, and decrefpicmarking operations
	if (pSurf) pSurf->Data.FrameOrder = m_nFramesRead;
	m_nFramesRead++;

	return sts;
}

void CEncodingPipeline::InsertIDR(bool bIsNextFrameIDR)
{
	if (bIsNextFrameIDR)
	{
		m_encCtrl.FrameType = MFX_FRAMETYPE_I | MFX_FRAMETYPE_IDR | MFX_FRAMETYPE_REF;
	}
	else
	{
		m_encCtrl.FrameType = MFX_FRAMETYPE_UNKNOWN;
	}
}

mfxStatus CEncodingPipeline::InitEncFrameParams(sTask* pTask)
{
	MSDK_CHECK_POINTER(pTask, MFX_ERR_NULL_PTR);

	return MFX_ERR_NONE;
}

mfxStatus CEncodingPipeline::AllocateSufficientBuffer(mfxBitstream* pBS)
{
	MSDK_CHECK_POINTER(pBS, MFX_ERR_NULL_PTR);
	MSDK_CHECK_POINTER(GetFirstEncoder(), MFX_ERR_NOT_INITIALIZED);

	mfxVideoParam par;
	MSDK_ZERO_MEMORY(par);

	// find out the required buffer size
	mfxStatus sts = GetFirstEncoder()->GetVideoParam(&par);
	MSDK_CHECK_STATUS(sts, "GetFirstEncoder failed");

	// reallocate bigger buffer for output
	sts = ExtendMfxBitstream(pBS, par.mfx.BufferSizeInKB * 1000);
	MSDK_CHECK_STATUS_SAFE(sts, "ExtendMfxBitstream failed", WipeMfxBitstream(pBS));

	return MFX_ERR_NONE;
}