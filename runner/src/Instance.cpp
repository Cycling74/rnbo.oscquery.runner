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

//helper for c-style callbakc from ossia, so we can have an index and the core object
struct Instance::ValueCallbackHelper {
	ValueCallbackHelper(RNBO::ParameterIndex i, RNBO::CoreObject *o) {
		index = i;
		core = o;
	}
	RNBO::ParameterIndex index;
	RNBO::CoreObject * core;
};

Instance::Instance(std::shared_ptr<PatcherFactory> factory, std::string name, NodeBuilder builder) : mPatcherFactory(factory) {
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
			auto h = std::make_shared<ValueCallbackHelper>(index, mCore.get());
			p.set_value_callback([](void* context, const opp::value& val) {
					ValueCallbackHelper * helper = reinterpret_cast<ValueCallbackHelper *>(context);
					//update from ossia
					if (helper && val.is_float())
						helper->core->setParameterValue(helper->index, val.to_float());
			}, h.get());
			mValueCallbackHelpers.emplace_back(h);
			mIndexToNode[index] = p;
			mNodes.push_back(p);
		}
		mNodes.push_back(params);
	});
}

Instance::~Instance() {
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

void Instance::processEvents() {
	mEventHandler->processEvents();
	mAudio->poll();
}

