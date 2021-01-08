#pragma once
#include <ossia-cpp/ossia-cpp98.hpp>
#include <functional>

//helper for c-style callback from ossia, so that we can use std func with captures.
class ValueCallbackHelper {
	public:
		//the function you give to libossia
		static void trampoline(void* context, const opp::value& val);

		ValueCallbackHelper(std::function<void(const opp::value& val)> func);
		void call(const opp::value& val);
	private:
		std::function<void(const opp::value& val)> mFunc;
};
