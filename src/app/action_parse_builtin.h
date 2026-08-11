#pragma once

#include "action_parse.h"

#if HAS_DISPLAY || HAS_BUTTON

void action_parse_volume(const JsonObject& obj, ButtonAction& act);
void action_serialize_volume(const ButtonAction& act, JsonObject obj);
void action_parse_brightness(const JsonObject& obj, ButtonAction& act);
void action_serialize_brightness(const ButtonAction& act, JsonObject obj);
void action_parse_system(const JsonObject& obj, ButtonAction& act);
void action_serialize_system(const ButtonAction& act, JsonObject obj);
void action_parse_music(const JsonObject& obj, ButtonAction& act);
void action_serialize_music(const ButtonAction& act, JsonObject obj);
void action_parse_screen(const JsonObject& obj, ButtonAction& act);
void action_serialize_screen(const ButtonAction& act, JsonObject obj);
void action_parse_key(const JsonObject& obj, ButtonAction& act);
void action_serialize_key(const ButtonAction& act, JsonObject obj);
void action_parse_delay(const JsonObject& obj, ButtonAction& act);
void action_serialize_delay(const ButtonAction& act, JsonObject obj);
void action_parse_mqtt(const JsonObject& obj, ButtonAction& act);
void action_serialize_mqtt(const ButtonAction& act, JsonObject obj);
void action_parse_timer(const JsonObject& obj, ButtonAction& act);
void action_serialize_timer(const ButtonAction& act, JsonObject obj);
void action_parse_sound_alert(const JsonObject& obj, ButtonAction& act);
void action_serialize_sound_alert(const ButtonAction& act, JsonObject obj);
void action_parse_notify(const JsonObject& obj, ButtonAction& act);
void action_serialize_notify(const ButtonAction& act, JsonObject obj);
void action_parse_ha_service(const JsonObject& obj, ButtonAction& act);
void action_serialize_ha_service(const ButtonAction& act, JsonObject obj);
void action_parse_visual_alert(const JsonObject& obj, ButtonAction& act);
void action_serialize_visual_alert(const ButtonAction& act, JsonObject obj);
void action_parse_cycle_pad(const JsonObject& obj, ButtonAction& act);
void action_serialize_cycle_pad(const ButtonAction& act, JsonObject obj);

#endif // HAS_DISPLAY || HAS_BUTTON