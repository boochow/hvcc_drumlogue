/*
 *  File: header.c
 *
 *  drumlogue unit header definition
 *
 */

#include "unit_drumlogue.h"

#ifndef PROJECT_DEV_ID
 #define PROJECT_DEV_ID (0x0U)
#endif

#ifndef PROJECT_UNIT_ID
 #define PROJECT_UNIT_ID (0x0U)
#endif

const __unit_header drumlogue_unit_header_t unit_header = {
  .common = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_{{unit_type}},
    .api = UNIT_API_VERSION,
    .dev_id = PROJECT_DEV_ID,
    .unit_id = PROJECT_UNIT_ID,
    .version = 0x00010000U,
    .name = "{{patch_name}}",
    {% if platform_name == "drumlogue" and unit_type == "synth" %}
    .num_presets = 0,
    {% set num_active_params = num_active_fixed_param + num_param + 1 %}
    {% else %}
    {% set num_active_params = num_active_fixed_param + num_param + 1 %}
    {% endif %}
    .num_params = {{ num_active_params }},
    .params = {
        // Format:
        // min, max, center, default, type, frac. bits, frac. mode, <reserved>, name
        {% if platform_name == "drumlogue" and unit_type == "synth" %}{% raw %}
        {0, 127, 48, 48, k_unit_param_type_midi_note, 0, 0, 0, {"Note"}},
        {% endraw %}
        {% endif %}
        {% if slfo is defined %}{% raw %}
        {0, 300, 100, 100, k_unit_param_type_hertz, 1, 1, 0, {"LFO Rate"}},
        {% endraw %}
        {% endif %}
        {% for i in range(1, 25 - num_active_params) %}
        {% set id = "param_id" ~ i %}
        {% if param[id] is defined %}
            {% if param[id]['disp_frac'] > 9 %}
        {{'{' ~ param[id]['disp_min'] | int}}, {{param[id]['disp_max'] | int}}, {{param[id]['disp_default'] | int}}, {{param[id]['disp_default'] | int}}, k_unit_param_type_strings, {{[param[id]['disp_frac'], 0] | max}}, 1, 0, {{ '{"' ~ param[id]['disp_name'] ~ '"}}' }}{% if not loop.last %},{{"\n"}}{% endif %}
            {% else %}
        {{'{' ~ param[id]['disp_min'] | int}}, {{param[id]['disp_max'] | int}}, {{param[id]['disp_default'] | int}}, {{param[id]['disp_default'] | int}}, k_unit_param_type_none, 0, 0, 0, {{ '{"' ~ param[id]['disp_name'] ~ '"}}' }}{% if not loop.last %},{{"\n"}}{% endif %}
            {% endif %}
        {% else %}{% raw %}
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}{% endraw %}
        {% if not loop.last %},{% endif %}
        {% endif %}
        {% if loop.last %}},{{"\n"}}
        {% else %}
        {% if (loop.index - 1 + num_active_params) % 4 == 3 %}

        {{"// Page " ~ (1 + (loop.index + 1) / 4)|int}}{% endif %}{% endif %}
        {% endfor %}
  }
};
