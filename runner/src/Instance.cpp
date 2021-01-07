#include <memory>
#include <iostream>
#include <functional>

#include "Instance.h"
#include "JackAudio.h"
#include "PatcherFactory.h"

using RNBO::ParameterIndex;
using RNBO::ParameterInfo;
using RNBO::ParameterType;
using RNBO::ParameterValue;

namespace {
	static const std::chrono::milliseconds command_wait_timeout(10);
}

//helper for c-style callback from ossia, so that we can use std func with captures.
class Instance::ValueCallbackHelper {
	public:
		ValueCallbackHelper(std::function<void(const opp::value& val)> func) : mFunc(func) { }
		void call(const opp::value& val) {
			mFunc(val);
		}
	private:
		std::function<void(const opp::value& val)> mFunc;
};

void Instance::valueCallbackTrampoline(void* context, const opp::value& val) {
	Instance::ValueCallbackHelper * helper = reinterpret_cast<Instance::ValueCallbackHelper *>(context);
	if (helper)
		helper->call(val);
}

Instance::Instance(std::shared_ptr<PatcherFactory> factory, std::string name, NodeBuilder builder) : mPatcherFactory(factory), mDataRefProcessCommands(true) {
	//RNBO is telling us we have a parameter update, tell ossia
	auto paramCallback = [this](RNBO::ParameterIndex index, RNBO::ParameterValue value) {
		auto it = mIndexToNode.find(index);
		if (it != mIndexToNode.end()) {
			it->second.set_value(value);
		}
	};
	mEventHandler = std::unique_ptr<EventHandler>(new EventHandler(paramCallback));
	mCore = std::make_shared<RNBO::CoreObject>(mPatcherFactory->createInstance(), mEventHandler.get());
	mAudio = std::unique_ptr<InstanceAudioJack>(new InstanceAudioJack(mCore, name, builder));

	builder([this](opp::node root) {
		//setup parameters
		auto params = root.create_child("params");
		params.set_description("parameter get/set");
		for (RNBO::ParameterIndex index = 0; index < mCore->getNumParameters(); index++) {
			ParameterInfo info;
			mCore->getParameterInfo(index, &info);
			//only supporting numbers right now
			//don't bind invisible or debug
			if (info.type != ParameterType::ParameterTypeNumber || !info.visible || info.debug)
				continue;

			//set parameter access, range, etc etc
			auto p = params.create_float(mCore->getParameterId(index));
			p.set_access(opp::access_mode::Bi);
			p.set_value(info.initialValue);
			p.set_min(info.min);
			p.set_max(info.max);
			p.set_bounding(opp::bounding_mode::Clip);

			//set the callback, using our helper
			auto h = std::make_shared<ValueCallbackHelper>([this, index](const opp::value& val) {
					if (val.is_float())
						mCore->setParameterValue(index, val.to_float());
			});
			p.set_value_callback(valueCallbackTrampoline, h.get());
			mValueCallbackHelpers.push_back(h);
			mIndexToNode[index] = p;
			mNodes.push_back(p);
		}
		mNodes.push_back(params);

		auto dataRefs = root.create_child("data_refs");
		for (auto index = 0; index < mCore->getNumExternalDataRefs(); index++) {
			std::string name(mCore->getExternalDataId(index));
			auto d = dataRefs.create_string(name);
			auto h = std::make_shared<ValueCallbackHelper>([this, index](const opp::value& val) {
					if (val.is_string())
						mDataRefCommandQueue.push(DataRefCommand(val.to_string(), index));
			});
			d.set_value_callback(valueCallbackTrampoline, h.get());
			mValueCallbackHelpers.push_back(h);
			mNodes.push_back(d);
		}
	});
	mDataRefThread = std::thread(&Instance::processDataRefCommands, this);
}

Instance::~Instance() {
	mDataRefProcessCommands.store(false);
	mDataRefThread.join();
	stop();
	mAudio.reset();
	mEventHandler.reset();
	mCore.reset();
	mPatcherFactory.reset();
}

void Instance::start() {
	mAudio->start();
}

void Instance::stop() {
	mAudio->stop();
}

void Instance::processDataRefCommands() {
	while (mDataRefProcessCommands.load()) {
		auto cmd = mDataRefCommandQueue.popTimeout(command_wait_timeout);
		if (!cmd.has_value())
			continue;
		//TODO
	}
}

void Instance::processEvents() {
	mEventHandler->processEvents();
	mAudio->poll();
}

