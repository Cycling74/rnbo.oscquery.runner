#pragma once

#include <ossia-cpp/ossia-cpp98.hpp>
#include <functional>
#include <memory>
#include <vector>

//helper for c-style callback from ossia, so that we can use std func with captures.
class ValueCallbackHelper {
	public:
		typedef std::function<void(const opp::value& val)> callback;
		//the function you give to libossia
		static void trampoline(void* context, const opp::value& val);

		//utility function to add callback to node and store it the callback object in the vector for lifetime control
		static void setCallback(
				opp::node& node,
				std::vector<std::shared_ptr<ValueCallbackHelper>>& storage,
				callback cb);

		ValueCallbackHelper(std::function<void(const opp::value& val)> func);
		void call(const opp::value& val);
	private:
		callback mFunc;
};
