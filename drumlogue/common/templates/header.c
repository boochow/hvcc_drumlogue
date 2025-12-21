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
    {% endif %}
    {% set num_all_params = num_fixed_param + num_conditional_param + num_param %}
    .num_params = {{ num_all_params }},
    .params = {
        // Format:
        // min, max, center, default, type, frac. bits, frac. mode, <reserved>, name
        // Page 1
        {% if platform_name == "drumlogue" and unit_type == "synth" %}
{% raw %}        {0, 127, 48, 48, k_unit_param_type_midi_note, 0, 0, 0, {"Note"}},
{% endraw %}
        {% endif %}
        {% for i in range(1, 25 - num_fixed_param - num_conditional_param) %}
        {% set param_count = loop.index - 1 + num_fixed_param + num_conditional_param %}
        {% if param_count % 4 == 0 %}
        {{"// Page " ~ (1 + param_count / 4)|int}}
        {% endif %}
        {% set id = "param_id" ~ i %}
        {% if param[id] is defined %}
            {% if param[id]['disp_frac'] > 9 or param[id]['type'] == '*pcm_index*' %}
        {{'{' ~ param[id]['disp_min'] | int}}, {{param[id]['disp_max'] | int}}, {{param[id]['disp_default'] | int}}, {{param[id]['disp_default'] | int}}, k_unit_param_type_strings, {{[param[id]['disp_frac'], 0] | max}}, 1, 0, {{ '{"' ~ param[id]['disp_name'] ~ '"}}' }}{% if not loop.last %},{{"\n"}}{% endif %}
            {% else %}
        {{'{' ~ param[id]['disp_min'] | int}}, {{param[id]['disp_max'] | int}}, {{param[id]['disp_default'] | int}}, {{param[id]['disp_default'] | int}}, {{param[id]['format'] }}, 0, 0, 0, {{ '{"' ~ param[id]['disp_name'] ~ '"}}' }}{% if not loop.last %},{{"\n"}}{% endif %}
            {% endif %}
        {% else %}
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}{% if not loop.last %},
        {% endif %}{% endif %}
        {% if loop.last %}},{{"\n"}}{% endif %}
        {% endfor %}
  }
};
