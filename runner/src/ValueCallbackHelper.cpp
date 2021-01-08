#include "ValueCallbackHelper.h"

void ValueCallbackHelper::trampoline(void* context, const opp::value& val) {
	ValueCallbackHelper * helper = reinterpret_cast<ValueCallbackHelper *>(context);
	if (helper)
		helper->call(val);
}

ValueCallbackHelper::ValueCallbackHelper(std::function<void(const opp::value& val)> func) : mFunc(func) { }

void ValueCallbackHelper::call(const opp::value& val) {
	mFunc(val);
}
