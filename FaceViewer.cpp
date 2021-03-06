// Copyright (c) 2010-2014, The Video Segmentation Project
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the The Video Segmentation Project nor the
//       names of its contributors may be used to endorse or promote products
//       derived from this software without specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// ---

#include "FaceViewer.h"
#include "landmarks_unit.h"
#include "face_segmentation_unit.h"
#include "video_writer_unit2.h"
#include <video_framework/video_reader_unit.h>
#include <video_framework/video_display_unit.h>
#include <video_framework/video_writer_unit.h>
#include <video_framework/video_pipeline.h>
#include <segmentation/segmentation_unit.h>

using namespace video_framework;

namespace segmentation
{
	FaceView::FaceView(const std::string& video_file, const std::string& seg_file,
		const std::string& landmarks_model_file, const std::string& output_path) :
		m_video_file(video_file), m_seg_file(seg_file),
		m_landmarks_model_file(landmarks_model_file),
		m_output_path(output_path)
	{
	}

	void FaceView::run()
	{
		//run_serial();
		run_parallel();
	}

	void FaceView::run_serial()
	{
		// Video Reader Unit
		VideoReaderUnit reader(VideoReaderOptions(), m_video_file);

		// Segmentation Reader Unit
		SegmentationReaderUnitOptions segOptions;
		segOptions.filename = m_seg_file;
		SegmentationReaderUnit segReader(segOptions);
		segReader.AttachTo(&reader);

		//VideoDisplayUnit display((VideoDisplayOptions()));
		//display.AttachTo(&reader);

		// Landmarks Unit
		LandmarksOptions landmarksOptions;
		landmarksOptions.landmarks_model_file = m_landmarks_model_file;
		std::shared_ptr<LandmarksUnit> landmarksUnit = LandmarksUnit::create(landmarksOptions);
		landmarksUnit->AttachTo(&segReader);

		// Face Segmentation Unit
		FaceSegmentationOptions faceSegOptions;
		FaceSegmentationUnit faceSegUnit(faceSegOptions);
		faceSegUnit.AttachTo(landmarksUnit.get());

		// Face Segmentation Renderer Unit
		FaceSegmentationRendererOptions faceSegRendererOptions;
		FaceSegmentationRendererUnit faceSegRendererUnit(faceSegRendererOptions);
		faceSegRendererUnit.AttachTo(&faceSegUnit);

		// Video Writer Unit
		VideoWriterOptions writer_options;
		VideoWriterUnit writer(writer_options, m_output_path);
		if (!m_output_path.empty())
		{
			writer.AttachTo(&faceSegRendererUnit);
		}

		if (!reader.PrepareProcessing())
			throw std::runtime_error("Video framework setup failed.");

		// Run with rate limitation.
		RatePolicy rate_policy;
		// Speed up playback for fun :)
		rate_policy.max_rate = 45;

		// This call will block and return when the whole has been displayed.
		if (!reader.RunRateLimited(rate_policy))
			throw std::runtime_error("Could not process video file.");
	}

	void FaceView::run_parallel()
	{
		std::vector<std::unique_ptr<VideoPipelineSource>> sources;
		std::vector<std::unique_ptr<VideoPipelineSink>> sinks;

		// Video Reader Unit
		VideoReaderOptions reader_options;
		std::unique_ptr<VideoReaderUnit> reader_unit(
			new VideoReaderUnit(reader_options, m_video_file));

		sinks.emplace_back(new VideoPipelineSink());
		sinks.back()->AttachTo(reader_unit.get());
		SourceRatePolicy srp;
		sources.emplace_back(new VideoPipelineSource(sinks.back().get(),
			nullptr, srp));

		// Segmentation Reader Unit
		SegmentationReaderUnitOptions seg_options;
		seg_options.filename = m_seg_file;
		std::unique_ptr<SegmentationReaderUnit> seg_reader(
			new SegmentationReaderUnit(seg_options));
		seg_reader->AttachTo(sources.back().get());

		// Landmarks Unit
		LandmarksOptions landmarks_options;
		landmarks_options.landmarks_model_file = m_landmarks_model_file;
		std::shared_ptr<LandmarksUnit> landmarks_unit = LandmarksUnit::create(landmarks_options);
		landmarks_unit->AttachTo(seg_reader.get());

		sinks.emplace_back(new VideoPipelineSink());
		sinks.back()->AttachTo(landmarks_unit.get());
		sources.emplace_back(new VideoPipelineSource(sinks.back().get()));

		// Face Segmentation Unit
		FaceSegmentationOptions face_seg_options;
		std::unique_ptr<FaceSegmentationUnit> face_seg_unit(
			new FaceSegmentationUnit(face_seg_options));
		face_seg_unit->AttachTo(sources.back().get());

		sinks.emplace_back(new VideoPipelineSink());
		sinks.back()->AttachTo(face_seg_unit.get());
		sources.emplace_back(new VideoPipelineSource(sinks.back().get()));

		// Face Segmentation Renderer Unit
		FaceSegmentationRendererOptions face_seg_renderer_options;
		std::unique_ptr<FaceSegmentationRendererUnit> face_seg_renderer_unit(
			new FaceSegmentationRendererUnit(face_seg_renderer_options));
		face_seg_renderer_unit->AttachTo(sources.back().get());
		/*
		// Video Writer Unit
		std::unique_ptr<VideoWriterUnit> writer_unit;
		if (!m_output_path.empty())
		{
			VideoWriterOptions writer_options;
			writer_options.bit_rate = 40000000;
			writer_options.fraction = 16;
			writer_unit.reset(new VideoWriterUnit(writer_options, m_output_path));
			writer_unit->AttachTo(face_seg_renderer_unit.get());
		}
		*/

		// Video Writer Unit 2
		std::unique_ptr<VideoWriterUnit2> writer_unit;
		if (!m_output_path.empty())
		{
			VideoWriter2Options writer_options;
			writer_unit.reset(new VideoWriterUnit2(writer_options, m_output_path));
			writer_unit->AttachTo(face_seg_renderer_unit.get());
		}

		// Prepare processing
		if (!reader_unit->PrepareProcessing())
			LOG(ERROR) << "Setup failed.";

		// Start the threads
		RatePolicy pipeline_policy;
		pipeline_policy.max_rate = 20;
		pipeline_policy.dynamic_rate = true;
		//pipeline_policy.startup_frames = 10;
		pipeline_policy.update_interval = 1;
		//pipeline_policy.queue_throttle_threshold = 10;
		// Guarantee that buffers never go empty in non-camera mode.
		pipeline_policy.dynamic_rate_scale = 1.1;
		VideoPipelineInvoker invoker;

		// First source is run rate limited.
		invoker.RunRootRateLimited(pipeline_policy, reader_unit.get());

		// Run last source in main thread.
		for (int k = 0; k < sources.size() - 1; ++k) {
			invoker.RunPipelineSource(sources[k].get());
		}

		sources.back()->Run();

		invoker.WaitUntilPipelineFinished();

		LOG(INFO) << "__FACE_SEGMENTATION_FINISHED__";
	}
}