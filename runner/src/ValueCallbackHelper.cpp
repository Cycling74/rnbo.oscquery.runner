#include "ValueCallbackHelper.h"

void ValueCallbackHelper::trampoline(void* context, const opp::value& val) {
	ValueCallbackHelper * helper = reinterpret_cast<ValueCallbackHelper *>(context);
	if (helper)
		helper->call(val);
}

void ValueCallbackHelper::setCallback(
		opp::node& node,
		std::vector<std::shared_ptr<ValueCallbackHelper>>& storage,
		callback cb) {
		auto h = std::make_shared<ValueCallbackHelper>(cb);
		node.set_value_callback(ValueCallbackHelper::trampoline, h.get());
		storage.push_back(h);
}

ValueCallbackHelper::ValueCallbackHelper(std::function<void(const opp::value& val)> func) : mFunc(func) { }

void ValueCallbackHelper::call(const opp::value& val) {
	mFunc(val);
}
