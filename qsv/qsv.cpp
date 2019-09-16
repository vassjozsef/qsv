#include <iostream>
#include <string>

#include "pipeline_encode.h"

int main(int argc, char** argv)
{
	if (argc != 6) {
		std::cerr << "Usage: " << argv[0] << " input_file_name output_file_name width height bitrate" << std::endl;
		return -1;
	}

	sInputParams params{};
	params.nWidth = std::stoi(argv[3]);
	params.nHeight = std::stoi(argv[4]);
	params.nBitRate = std::stoi(argv[5]);
	params.FileInputFourCC = MFX_FOURCC_I420;
	params.InputFiles = { argv[1] };
	params.dstFileBuff = { argv[2] };
	params.dFrameRate = 30;

	std::auto_ptr<CEncodingPipeline> pPipeline;
	pPipeline.reset(new CEncodingPipeline());

	MSDK_CHECK_POINTER(pPipeline.get(), MFX_ERR_MEMORY_ALLOC);
	auto sts = pPipeline->Init(&params);
	MSDK_CHECK_STATUS(sts, "pPipeline->Init failed");

	std::cout << "Processing started" << std::endl;

	for (;;)
	{
		sts = pPipeline->Run();

		if (MFX_ERR_DEVICE_LOST == sts || MFX_ERR_DEVICE_FAILED == sts)
		{
			std::cout << "ERROR: Hardware device was lost or returned an unexpected error. Recovering..." << std::endl;

			sts = pPipeline->ResetMFXComponents(&params);
			MSDK_CHECK_STATUS(sts, "pPipeline->ResetMFXComponents failed");
			continue;
		}
		else
		{
			MSDK_CHECK_STATUS(sts, "pPipeline->Run failed");
			break;
		}
	}

	pPipeline->Close();

	std::cout << "Processing finished" << std::endl;

	return 0;
}
