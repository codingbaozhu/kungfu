//
// Created by Keren Dong on 2020/2/25.
//

#ifndef KUNGFU_NODE_CONFIGURATION_H
#define KUNGFU_NODE_CONFIGURATION_H

#include <napi.h>

#include <kungfu/yijinjing/io.h>
#include <kungfu/yijinjing/practice/config_store.h>

#include "operators.h"

namespace kungfu::node
{
    class ConfigStore : public Napi::ObjectWrap<ConfigStore>
    {
    public:
        explicit ConfigStore(const Napi::CallbackInfo &info);

        ~ConfigStore();

        Napi::Value SetConfig(const Napi::CallbackInfo &info);

        Napi::Value GetConfig(const Napi::CallbackInfo &info);

        Napi::Value GetAllConfig(const Napi::CallbackInfo &info);

        Napi::Value RemoveConfig(const Napi::CallbackInfo &info);

        static void Init(Napi::Env env, Napi::Object exports);

    private:
        serialize::JsSet set;
        yijinjing::data::locator_ptr locator_;
        yijinjing::practice::config_store cs_;
        static Napi::FunctionReference constructor;
    };
}

#endif //KUNGFU_NODE_CONFIGURATION_H