#include "include/gamepads_linux/gamepads_linux_plugin.h"

#include <flutter_linux/flutter_linux.h>

#include <iostream>
#include <optional>
#include <map>
#include <sstream>
#include <thread>

#include "gamepad_connection_listener.h"
#include "gamepad_listener.h"

#define GAMEPADS_LINUX_PLUGIN(obj)                                       \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), gamepads_linux_plugin_get_type(), \
                                GamepadsLinuxPlugin))

struct _GamepadsLinuxPlugin {
    GObject parent_instance;
};

G_DEFINE_TYPE(GamepadsLinuxPlugin, gamepads_linux_plugin, g_object_get_type())

static FlMethodChannel *channel;

bool keep_reading_events = false;

struct ConnectedGamepad {
    std::string name;
    bool alive = false;
    std::thread* listener = nullptr;
};

std::map<std::string, ConnectedGamepad> gamepads = {};

static void emit_gamepad_event(const gamepad_listener::GamepadEvent& event) {
    if (channel) {
        g_autoptr(FlValue) map = fl_value_new_map();
        fl_value_set_string(map, "gamepadId", fl_value_new_string(event.gamepad_id.c_str()));
        fl_value_set_string(map, "value", fl_value_new_string(event.value.c_str()));
        fl_method_channel_invoke_method(channel, "onGamepadEvent", map, nullptr, nullptr, nullptr);
    }
}

static void respond_not_found(FlMethodCall *method_call) {
    g_autoptr(FlMethodResponse) response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
    fl_method_call_respond(method_call, response, nullptr);
}

static void respond(FlMethodCall *method_call, FlValue* value) {
    g_autoptr(FlMethodResponse) response = FL_METHOD_RESPONSE(fl_method_success_response_new(value));
    fl_method_call_respond(method_call, response, nullptr);
}

static void gamepads_linux_plugin_handle_method_call(GamepadsLinuxPlugin *self, FlMethodCall *method_call) {
    const gchar *method = fl_method_call_get_name(method_call);
    // FlValue *args = fl_method_call_get_args(method_call);

    if (strcmp(method, "listGamepads") == 0) {
        g_autoptr(FlValue) list = fl_value_new_list();
        for (auto [key, gamepad] : gamepads) {
            g_autoptr(FlValue) map = fl_value_new_map();
            fl_value_set(map, fl_value_new_string("id"), fl_value_new_string(key.c_str()));
            fl_value_append(list, map);
        }
        respond(method_call, list);
    } else {
        respond_not_found(method_call);
    }

}

static void method_call_cb([[maybe_unused]] FlMethodChannel *flutter_channel, FlMethodCall *method_call, gpointer user_data) {
    GamepadsLinuxPlugin *plugin = GAMEPADS_LINUX_PLUGIN(user_data);
    gamepads_linux_plugin_handle_method_call(plugin, method_call);
}

void gamepads_linux_plugin_register_with_registrar(FlPluginRegistrar *registrar) {
    GamepadsLinuxPlugin *plugin = GAMEPADS_LINUX_PLUGIN(g_object_new(gamepads_linux_plugin_get_type(), nullptr));

    g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
    channel = fl_method_channel_new(fl_plugin_registrar_get_messenger(registrar), "xyz.luan/gamepads", FL_METHOD_CODEC(codec));

    fl_method_channel_set_method_call_handler(channel, method_call_cb, g_object_ref(plugin), g_object_unref);

    g_object_unref(plugin);
}

void process_connection_event(
    const std::string& device,
    bool* alive
) {
    gamepad_listener::listen(
        device,
        alive,
        [](const gamepad_listener::GamepadEvent& value) { emit_gamepad_event(value); }
    );
}

void event_loop_start() {
    gamepad_connection_listener::listen(
        &keep_reading_events,
        [](const gamepad_connection_listener::ConnectionEvent& event) {
            std::string key = event.device;
            std::optional<ConnectedGamepad> existingGamepad = gamepads[key];
            if (event.type == gamepad_connection_listener::ConnectionEventType::CONNECTED) {
                if (existingGamepad && existingGamepad->alive) {
                    return;
                }

                std::cout << "Gamepad connected " << key << std::endl;
                gamepads[key] = {key, true, nullptr};

                std::thread input_thread(process_connection_event, key, &(gamepads[key].alive));
                gamepads[key].listener = &input_thread;
                input_thread.detach();
            } else {
                std::cout << "Gamepad disconnected " << key << std::endl;
                if (existingGamepad) {
                    gamepads[key].alive = false;
                    gamepads.erase(key);
                }
            }
        }
    );
}

static void gamepads_linux_plugin_dispose(GObject *object) {
    keep_reading_events = false;
    G_OBJECT_CLASS(gamepads_linux_plugin_parent_class)->dispose(object);
}

static void gamepads_linux_plugin_class_init(GamepadsLinuxPluginClass *klass) {
    G_OBJECT_CLASS(klass)->dispose = gamepads_linux_plugin_dispose;
}

static void gamepads_linux_plugin_init(GamepadsLinuxPlugin *self) {
    keep_reading_events =  true;

    std::thread event_loop_thread(event_loop_start);
    event_loop_thread.detach();
}