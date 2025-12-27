{% if platform_name == "drumlogue" %}
#include <errno.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <climits>
#include <cstring>
#include <cstdio>

#include "unit_drumlogue.h"
{% endif %}

#include "Heavy_{{patch_name}}.h"

#ifndef HV_MSGPOOLSIZE
 #define HV_MSGPOOLSIZE {{msg_pool_size_kb}}
#endif
#ifndef HV_INPUTQSIZE
 #define HV_INPUTQSIZE {{input_queue_size_kb}}
#endif
#ifndef HV_OUTPUTQSIZE
 #define HV_OUTPUTQSIZE {{output_queue_size_kb}}
#endif
{% if unit_type == "synth" %}

#define HV_HASH_NOTEIN          0x67E37CA3
#define HV_HASH_BENDIN          0x3083F0F7
#define HV_HASH_TOUCHIN         0x553925BD
#define HV_HASH_POLYTOUCHIN     0xBC530F59

{% endif %}
static bool stop_unit_param;
static HeavyContextInterface* hvContext;

typedef enum {
    // if platform name in [loguesdkv1, nts1mkii, nts3kaoss]
    {% if pitch is defined or pitch_note is defined %}
    k_user_unit_param_pitch,
    {% endif %}
    // endif
    {% if unit_type == "synth" %}
    k_user_unit_param_note,
    {% endif %}
    {% if slfo is defined %}
    k_user_unit_param_lfo,
    {% endif %}
    {% for i in range(1, 25 - num_fixed_param) %}
    {% set id = "param_id" ~ i %}
    k_user_unit_{{id}},
    {% endfor %}
    k_num_user_unit_param_id
} user_unit_param_id_t;

static unit_runtime_desc_t s_desc;
static int32_t params[k_num_user_unit_param_id];
{% if slfo is defined %}
#define LFO_DEFAULTFREQ 0
static dsp::SimpleLFO s_lfo;
static const float s_fs_recip = 1.f / 48000.f;                                 
{% endif %}
{% if noteon_trig is defined %}
static bool noteon_trig_dirty;
{% endif %}
{% if noteoff_trig is defined %}
static bool noteoff_trig_dirty;
{% endif %}
{% if sys_tempo is defined %}
static uint32_t sys_tempo;
static bool sys_tempo_dirty;
{% endif %}
{% if unit_type == "synth" %}
#ifndef DEFAULT_MIDI_CH
#define DEFAULT_MIDI_CH (0.)
#endif

typedef struct note_event {
    uint8_t note;
    uint8_t velocity;
} note_event_t;

#define FIFO_SIZE 128

typedef struct note_event_queue {
    note_event_t events[FIFO_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} note_event_queue_t;

static note_event_queue_t note_event_q;

static inline void note_event_queue_init(note_event_queue_t *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

static inline bool note_event_enqueue(note_event_queue_t *q, uint8_t n, uint8_t v) {
    if (q->count >= FIFO_SIZE) {
        return false;
    }
    q->events[q->head].note = n;
    q->events[q->head].velocity = v;
    q->head++;
    if (q->head >= FIFO_SIZE) {
        q->head = 0;
    }
    q->count++;
    return true;
}

static inline bool note_event_dequeue(note_event_queue_t *q, note_event_t *out) {
    if (q->count == 0) {
        return false;
    }
    *out = q->events[q->tail];
    q->tail++;
    if (q->tail >= FIFO_SIZE) {
        q->tail = 0;
    }
    q->count--;
    return true;
}

static uint16_t bendin;
static bool bendin_dirty = false;
static uint8_t touchin;
static bool touchin_dirty = false;
{% endif %}
{% for i in range(1, 25) %}
    {% set id = "param_id" ~ i %}
    {% if param[id] is defined %}
        {% if param[id]['type'] == 'int' %}
static int32_t {{param[id]['name']}};
        {% elif param[id]['type'] == 'float' %}
static float {{param[id]['name']}};
        {% elif param[id]['type'] == '*pcm_index*' %}
static int32_t {{param[id]['name']}};
        {% endif %}
    {% endif %}
{% endfor %}
{% if num_param > 0 %}
static bool param_dirty[{{num_param}}];
{% endif %}
{% for key, entry in table.items() %}
static float * table_{{ key }};
{% if entry.type == "sample" %}
{% set tablename = key[:-2] %}
static uint8_t {{ tablename }}_bank;
static bool {{ tablename }}_bank_dirty = false;
static int32_t {{ tablename }}_index;
static bool {{ tablename }}_index_dirty = false;
static bool {{ tablename }}_guard = 0;
static bool {{ tablename }}_guard_dirty = false;
static int32_t {{ tablename }}_chan = 1;
static bool {{ tablename }}_chan_dirty = false;
{% endif %}
{% endfor %}
{% if unit_type == "synth" %}

static void sendHook(HeavyContextInterface *c, const char *sendName, unsigned int sendHash, const HvMessage *m) {
    switch (sendHash) {
    {% for key, entry in table.items() %}
    {% if entry.type == "sample" %}
    {% set tablename = key[:-2] %}
    {% set param = soundloader[key]['set_param'] %}
    {% if out_param[param] is defined %}
    case HV_{{ patch_name|upper }}_PARAM_OUT_{{ param |upper}}:
        if (hv_msg_getNumElements(m) == 2) {
            if (hv_msg_isSymbol(m, 0) && hv_msg_isFloat(m, 1)) {
                const char *symbol = hv_msg_getSymbol(m, 0);
                float value = hv_msg_getFloat(m, 1);
                if (std::strcmp(symbol, "bank") == 0) {
                    if ({{ tablename }}_bank != value) {
                        {{ tablename }}_bank = value;
                        {{ tablename }}_bank_dirty = true;
                    }
                } else if (std::strcmp(symbol, "index") == 0) {
                    if ({{ tablename }}_index != value) {
                        {{ tablename }}_index = value;
                        {{ tablename }}_index_dirty = true;
                    }
                } else if (std::strcmp(symbol, "guard") == 0) {
                    if ({{ tablename }}_guard != (value != 0)) {
                        {{ tablename }}_guard = (value != 0);
                        {{ tablename }}_guard_dirty = true;
                    }
                } else if (std::strcmp(symbol, "chan") == 0) {
                    if ({{ tablename }}_chan != value) {
                        {{ tablename }}_chan = value;
                        {{ tablename }}_chan_dirty = true;
                    }
                }
            }
        }
        break;
    {% endif %}
    {% endif %}
    {% endfor %}
    default:
        break;
    }
}
{% endif %}
{% if platform_name == "drumlogue" %}
#ifdef PRINTHOOK
// This is only for debugging purposes
#define PRINTBUFSIZE 20
static char printBuf[PRINTBUFSIZE][32];
static int32_t printBufIndex = 0;

static void printHook(HeavyContextInterface *c, const char *printName, const char *str, const HvMessage *m) {
    strncpy(printBuf[printBufIndex], str, 32);
    printBufIndex = (printBufIndex + 1) % PRINTBUFSIZE;
}
/*
  the printBuf[] can be used in unit_get_param_str_value().
  use [print] object in the patch or
  use printHook() like below to print your C variable
  {
      char s[32];
      std::snprintf(s, 32, "%d", your_variable);
      printHook(hvContext, "", s, NULL);
  }
 */
#endif
{% endif %}

__unit_callback int8_t unit_init(const unit_runtime_desc_t * desc)
{
    stop_unit_param = true;
    {% if slfo is defined %}
    s_lfo.reset();
    s_lfo.setF0(LFO_DEFAULTFREQ, s_fs_recip);
    {% endif %}
    {% if touch_began is defined or touch_moved is defined or touch_ended is defined or touch_stationary is defined or touch_cancelled is defined %}
    touch_event.dirty = false;
    {% endif %}
    {% for i in range(1, 25) %}
      {% set id = "param_id" ~ i %}
      {% if param[id] is defined %}
        {% if param[id]['type'] == 'float' %}
    {{param[id]['name']}} = {{param[id]['default']}};
    params[k_user_unit_{{id}}] = {{ param[id]['disp_default'] }};
        {% elif param[id]['type'] == 'int' %}
    {{param[id]['name']}} = {{param[id]['default'] | int}};
    params[k_user_unit_{{id}}] = {{param[id]['name']}};
        {% elif param[id]['type'] == '*pcm_index*' %}
    {{param[id]['name']}} = {{param[id]['default'] | int}};
    params[k_user_unit_{{id}}] = {{param[id]['name']}};
        {% endif %}
    param_dirty[k_user_unit_{{id}}] = true;
      {% endif %}
    {% endfor %}
    {% if unit_type == "synth" %}
    params[k_user_unit_param_note] = 48;
    {% endif %}

    if (!desc)
      return k_unit_err_undef;

    if (desc->target != unit_header.common.target)
      return k_unit_err_target;

    if (!UNIT_API_IS_COMPAT(desc->api))
      return k_unit_err_api_version;

    if (desc->samplerate != 48000)
      return k_unit_err_samplerate;

    {% if unit_type == "synth" %}
    // todo: include loguesdk_v1 osc here
    if (desc->output_channels < {{num_output_channels}})
      return k_unit_err_geometry;
    {% else %}
    if (desc->input_channels != 2 || desc->output_channels < {{num_output_channels}})
      return k_unit_err_geometry;
    {% endif %}

#ifdef RENDER_HALF
    hvContext = hv_{{patch_name}}_new_with_options(24000, HV_MSGPOOLSIZE, HV_INPUTQSIZE, HV_OUTPUTQSIZE);
#else
    hvContext = hv_{{patch_name}}_new_with_options(48000, HV_MSGPOOLSIZE, HV_INPUTQSIZE, HV_OUTPUTQSIZE);
#endif
    {% for key, entry in table.items() %}
    table_{{ key }} = hv_table_getBuffer(hvContext, HV_{{patch_name|upper}}_TABLE_{{key|upper}});
    {% endfor %}

    s_desc = *desc;
    {% if unit_type == "synth" %}
    note_event_queue_init(&note_event_q);
    hv_setSendHook(hvContext, sendHook);
    {% endif %}

    return k_unit_err_none;
}

__unit_callback void unit_render(const float * in, float * out, uint32_t frames)
{
#ifdef RENDER_HALF
    float buffer[frames];
    static float last_buf_l = 0.f;
    static float last_buf_r = 0.f;
    float * __restrict p = buffer;
    float * __restrict y = out;
    const float * y_e = y + {{num_output_channels}} * frames;
#endif

    stop_unit_param = false;

    {% if pitch is defined %} 
    const float pitch = osc_w0f_for_note(params[k_user_unit_param_pitch]>>3, (params[k_user_unit_param_pitch] & 0x7)<<5) * k_samplerate;
    hv_sendFloatToReceiver(hvContext, HV_{{patch_name|upper}}_PARAM_IN_PITCH, pitch);
    {% endif %}
    {% if pitch_note is defined %}
    hv_sendFloatToReceiver(hvContext, HV_{{patch_name|upper}}_PARAM_IN_PITCH_NOTE, params[k_user_unit_param_pitch]>>3);
    {% endif %}
    {% if slfo is defined %}
    s_lfo.setF0(0.1 * params[k_user_unit_param_lfo], s_fs_recip);
    if (params[k_user_unit_param_lfo] == 0) {
        s_lfo.reset();
    }
    hv_sendFloatToReceiver(hvContext, HV_{{patch_name|upper}}_PARAM_IN_SLFO, 1.f - s_lfo.triangle_uni());
    for(int i = 0 ; i < frames ; i++) {
        s_lfo.cycle();
    }
    {% endif %}
    {% if touch_began is defined or touch_moved is defined or touch_ended is defined or touch_stationary is defined or touch_cancelled is defined %}
    if (touch_event.dirty) {
        touch_event.dirty = false;
        switch(touch_event.phase) {
        case k_unit_touch_phase_began:
            {% if touch_began is defined %}
            hv_sendMessageToReceiverV(hvContext, HV_{{patch_name|upper}}_PARAM_IN_TOUCH_BEGAN, 0, "ff", touch_event.x, touch_event.y);
            {% endif %}
            break;
        case k_unit_touch_phase_moved:
            {% if touch_moved is defined %}
            hv_sendMessageToReceiverV(hvContext, HV_{{patch_name|upper}}_PARAM_IN_TOUCH_MOVED, 0, "ff", touch_event.x, touch_event.y);
            {% endif %}
            break;
        case k_unit_touch_phase_ended:
            {% if touch_ended is defined %}
            hv_sendMessageToReceiverV(hvContext, HV_{{patch_name|upper}}_PARAM_IN_TOUCH_ENDED, 0, "ff", touch_event.x, touch_event.y);
            {% endif %}
            break;
        case k_unit_touch_phase_stationary:
            {% if touch_stationary is defined %}
            hv_sendMessageToReceiverV(hvContext, HV_{{patch_name|upper}}_PARAM_IN_TOUCH_STATIONARY, 0, "ff", touch_event.x, touch_event.y);
            {% endif %}
            break;
        case k_unit_touch_phase_cancelled:
            {% if touch_cancelled is defined %}
            hv_sendMessageToReceiverV(hvContext, HV_{{patch_name|upper}}_PARAM_IN_TOUCH_CANCELLED, 0, "ff", touch_event.x, touch_event.y);
            {% endif %}
            break;
        default:
            break;
        }
    }
    {% endif %}
    {% if unit_type == "synth" %}
    note_event_t e;
    while(note_event_dequeue(&note_event_q, &e)) {
        hv_sendMessageToReceiverV(hvContext, HV_HASH_NOTEIN, 0, "fff", (float) e.note, (float) e.velocity, DEFAULT_MIDI_CH);
    }
    if (bendin_dirty) {
        if (hv_sendMessageToReceiverV(hvContext, HV_HASH_BENDIN, 0, "ff", (float) bendin, DEFAULT_MIDI_CH)) {
            bendin_dirty = false;
        }
    }
    if (touchin_dirty) {
        if (hv_sendMessageToReceiverV(hvContext, HV_HASH_TOUCHIN, 0, "ff", (float) touchin, DEFAULT_MIDI_CH)) {
            touchin_dirty = false;
        }
    }
    {% endif %}
    {% for i in range(1, 25 - num_fixed_param) %}
    {% set id = "param_id" ~ i %}
    {% if param[id] is defined %}
    if (param_dirty[k_user_unit_{{id}}]) {
        if (hv_sendFloatToReceiver(hvContext, HV_{{patch_name|upper}}_PARAM_IN_{{param[id]['name']|upper}}, {{param[id]['name']}})) {
            param_dirty[k_user_unit_{{id}}] = false;
        }
    }
    {% endif %}
    {% endfor %}
    {% for key, entry in table.items() %}
    {% if entry.type == "random" %}
    uint32_t table_len = hv_table_getLength(hvContext, HV_{{patch_name|upper}}_TABLE_{{key|upper}});
    for (int i = 0; i < table_len ; i++) {
        float r = rand() / (float)RAND_MAX;
        table_{{ key }}[i] = 2. * r - 1.;
    }
    {% endif %}
    {% endfor %}
    {% if noteon_trig is defined %}
    if (noteon_trig_dirty) {
        if (hv_sendBangToReceiver(hvContext, HV_{{patch_name|upper}}_PARAM_IN_NOTEON_TRIG)) {
            noteon_trig_dirty = false;
        }
    }
    {% endif %}
    {% if noteoff_trig is defined %}
    if (noteoff_trig_dirty) {
        if (hv_sendBangToReceiver(hvContext, HV_{{patch_name|upper}}_PARAM_IN_NOTEOFF_TRIG)) {
            noteoff_trig_dirty = false;
        }
    }
    {% endif %}
    {% if sys_tempo is defined %}
    if (sys_tempo_dirty) {
        if (hv_sendFloatToReceiver(hvContext, HV_{{patch_name|upper}}_PARAM_IN_SYS_TEMPO, (sys_tempo >> 16) + 1.52587890625e-005f * (sys_tempo & 0xffff))) {
            sys_tempo_dirty = false;
        }
    }
    {% endif %}
    {% if metro_4ppqn is defined %}
    if (metro_4ppqn) {
        if (hv_sendBangToReceiver(hvContext, HV_{{patch_name|upper}}_PARAM_IN_METRO_4PPQN)) {
            metro_4ppqn = false;
        }
    }
    {% endif %}
    {% if unit_type == "synth" %}
    {% for key, entry in table.items() %}
    {% if entry.type == "sample" %}
    {% set tablebank = key[:-2] ~ '_bank' %}
    {% set tableindex = key[:-2] ~ '_index' %}
    {% set tableguard = key[:-2] ~ '_guard' %}
    {% set tablechan = key[:-2] ~ '_chan' %}
    if ({{ tablebank }}_dirty || {{ tableindex }}_dirty || {{ tableguard }}_dirty || {{ tablechan }}_dirty) {
        const sample_wrapper_t *sample;
        const int32_t index_max = s_desc.get_num_samples_for_bank({{ tablebank }});

        {{ tablebank }}_dirty = false;
        {{ tableindex }}_dirty = false;
        {{ tableguard }}_dirty = false;
        {{ tablechan }}_dirty = false;
        if ({{ tableindex }} >= index_max) {
            {{ tableindex }} = index_max;
        }

        sample = s_desc.get_sample({{ tablebank }}, {{ tableindex }});
        if ((sample != NULL) && (sample->frames > 0)) {
            const size_t length = sample->frames;
            size_t newsize = length;
            size_t offset = 0;
            if ({{ tableguard }}) {
                newsize += 3;
                offset = 1;
            }
            if ({{ tablechan }} == 2) {
                newsize *= 2;
            }
            uint32_t table_len = hv_table_getLength(hvContext, HV_{{patch_name|upper}}_TABLE_{{key|upper}});
            if (table_len < newsize) {
                hv_table_setLength(hvContext, HV_{{patch_name|upper}}_TABLE_{{key|upper}}, newsize);
                table_{{ key }} = hv_table_getBuffer(hvContext, HV_{{patch_name|upper}}_TABLE_{{key|upper}});
                table_len = hv_table_getLength(hvContext, HV_{{patch_name|upper}}_TABLE_{{key|upper}});
                if (table_len < newsize) {
                    newsize = table_len;
                }
            }
            if (sample->channels == 1) {
                memcpy(table_{{ key }} + offset, sample->sample_ptr, sizeof(float) * length);
                if ({{ tablechan }} == 2) {
                    memcpy(table_{{ key }} + newsize / 2 + offset, sample->sample_ptr, sizeof(float) * length);
                }
            } else if (sample->channels == 2) {
                float *p = table_{{ key }} + offset;
                const float *l = sample->sample_ptr;
                const float *r = sample->sample_ptr + 1;
                if ({{ tablechan }} == 1) {
                    for(uint32_t i = 0; i < length; i++, l+=2, r+=2) {
                        float s = (*l + *r) * 0.5;
                        s = s > 1.f ? 1.f : (s < -1.f ? -1.f : s);
                        *p++ = s;
                    }
                } else if ({{ tablechan }} == 2) {
                    float *q = table_{{ key }} + newsize / 2 + offset;
                    for(uint32_t i = 0; i < length; i++, l+=2, r+=2) {
                        *p++ = *l;
                        *q++ = *r;
                    }
                }
            }
            if ({{ tableguard }}) {
                table_{{ key }}[0] = table_{{ key }}[length];
                table_{{ key }}[length + 1] = table_{{ key }}[1];
                table_{{ key }}[length + 2] = table_{{ key }}[2];
                if ({{ tablechan }} == 2) {
                    size_t top = length + 3;
                    table_{{ key }}[top] = table_{{ key }}[top + length];
                    table_{{ key }}[top + length + 1] = table_{{ key }}[top + 1];
                    table_{{ key }}[top + length + 2] = table_{{ key }}[top + 2];
                }
            }
            hv_sendFloatToReceiver(hvContext, HV_{{patch_name|upper}}_PARAM_IN_{{ soundloader[key]['size_param']|upper}}, (float) length);
        }
    }

    {% set selector = soundloader[key]['indexMenu_param'] %}
    {% if selector['name'] is defined %}
    if ({{ selector }}_dirty) {
        if (hv_sendFloatToReceiver(hvContext, HV_{{patch_name|upper}}_PARAM_IN_{{ selector|upper}}, (float) {{ selector }})) {
            {{ selector }}_dirty = false;
        }
    }
    {% endif %}
    {% endif %}
    {% endfor %}
    {% endif %}
#ifdef RENDER_HALF
    {% if class_name == 'Nts3_bgfx' %}
    const unit_runtime_genericfx_context_t *ctxt = static_cast<const unit_runtime_genericfx_context_t *>(s_desc.hooks.runtime_context);
    hv_processInlineInterleaved(hvContext, (float *) ctxt->get_raw_input(), buffer, frames >> 1);
    {% else %}
    hv_processInlineInterleaved(hvContext, (float *) in, buffer, frames >> 1);
    {% endif %}
    for(int i = 0; y!= y_e; i++) {
        if (i & 1) {
            last_buf_l = *p++;
            last_buf_r = *p++;
            *(y++) = last_buf_l;
            *(y++) = last_buf_r;
        } else {
            *(y++) = (*p + last_buf_l) * 0.5;
            *(y++) = (*p + last_buf_r) * 0.5;
        }
    }
#else
    {% if class_name == 'Nts3_bgfx' %}
    const unit_runtime_genericfx_context_t *ctxt = static_cast<const unit_runtime_genericfx_context_t *>(s_desc.hooks.runtime_context);
    hv_processInlineInterleaved(hvContext, (float *) ctxt->get_raw_input(), out, frames);
    {% else %}
    hv_processInlineInterleaved(hvContext, (float *) in, out, frames);
    {% endif %}
#endif
    {% if num_output_channels == 1 %}
    if (s_desc.output_channels == 2) {
        float * p = out + frames;
        float * y = p + frames;
        for(; y > p ; ) {
            *(--y) = *(--p);
            *(--y) = *p;
        }
    }
    {% endif %}
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value)
{
    {% if platform_name != "drumlogue" %}
    float knob_f = param_val_to_f32(value);
    {% endif %}

    if (stop_unit_param) {
        return; // avoid all parameters to be zero'ed after unit_init()
    }
    params[id] = value;
    switch(id){
    {% for i in range(1, 25) %}
    {% set id = "param_id" ~ i %}
    {% if param[id] is defined %}
    {% set param_type = param[id]['type'] %}
    {% set param_name = param[id]['name'] %}
    case k_user_unit_{{id}}:
        {% if param_type == 'int' %}
        {{ param_name }} = value;
        param_dirty[k_user_unit_{{id}}] = true;
        {% elif param_type == 'float' %}
        {{ param_name }} = {{ param[id]['min'] }} + value * {{ (param[id]['max'] - param[id]['min']) / param[id]['disp_max'] }};
        param_dirty[k_user_unit_{{id}}] = true;
        {% elif param_type == '*pcm_index*' %}
        if ({{ param_name }} != value) {
            {{ param_name }} = value;
            param_dirty[k_user_unit_{{id}}] = true;
        }
        {% endif %}
        break;
    {% endif %}
    {% endfor %}
    default:
      break;
    }
}

__unit_callback int32_t unit_get_param_value(uint8_t id) {
    return params[id];
}

{% if noteon_trig is defined %} 
static void note_on()
{
    noteon_trig_dirty = true;
}
{% endif %}

{% if noteoff_trig is defined %} 
static void note_off()
{
    noteoff_trig_dirty = true;
}
{% endif %}

__unit_callback void unit_teardown() {
}

__unit_callback void unit_reset() {
}

__unit_callback void unit_resume() {
}

__unit_callback void unit_suspend() {
}

{% if platform_name == "drumlogue" %}
static void format_number(char *p_str, size_t len, float f, char *format) {
    char formatstr[8];

    std::snprintf(p_str, len, "%.3f", f);
    char *p = p_str + strlen(p_str) - 1;
    while (p > p_str && *p == '0') {
        p--;
    }
    if (*p == '.') {
        p--;
    }
    std::strncpy(formatstr, (format ? format : ""), 8); // allow format == null
    std::strncpy(++p, formatstr, 8);
}

static char *formatstr(uint8_t type) {
    switch(type) {
    case k_unit_param_type_percent:
        return "%";
    case k_unit_param_type_db:
        return "db";
    case k_unit_param_type_cents:
        return "C";
    case k_unit_param_type_hertz:
        return "Hz";
    case k_unit_param_type_khertz:
        return "kHz";
    case k_unit_param_type_msec:
        return "ms";
    case k_unit_param_type_sec:
        return "s";
    default:
        break;
    }
    return nullptr;
}

__unit_callback const char * unit_get_param_str_value(uint8_t id, int32_t value \
) {
    static char p_str[16];
    float fvalue;
    static char empty_str[10] = "000:---";

    switch(id) {
    {% for i in range(1, 25) %}
    {% set id = "param_id" ~ i %}
    {% if param[id] is defined and param[id]['disp_frac'] > 0 %}
    case k_user_unit_{{id}}: {
        char *s;
        fvalue = {{ param[id]['min' ]}} + value * ({{ param[id]['max'] }} - {{ param[id]['min'] }}) / {{ param[id]['disp_max'] }};
        s = formatstr({{ param[id]['format'] }});
        format_number(p_str, sizeof(p_str), fvalue, s);
        return p_str;
        break;
    }
    {% endif %}
    {% if param[id] is defined and param[id]['type'] == '*pcm_index*' %}
    {% set tablename = param[id]['table'] %}
    {% set tablebank = tablename[:-2] ~ '_bank' %}
    case k_user_unit_{{id}}: {
        const sample_wrapper_t *sample;
        const int32_t index_max = s_desc.get_num_samples_for_bank({{ tablebank }});
        static char samplename[12];
        size_t namelen;

        if (value < index_max) {
            sample = s_desc.get_sample({{ tablebank }}, value);
            namelen = strlen(sample->name);
            if (namelen < 8) {
                return sample->name;
            } else {
                strncpy(samplename, sample->name, 3);
                samplename[3] = '.';
                samplename[4] = '.';
                strncpy(&samplename[5], (sample->name + namelen - 3), 4);
                return samplename;
            }
        } else {
            empty_str[0] = (char) 0x30 + ((value % 1000) / 100);
            empty_str[1] = 0x30 + (value % 100) / 10;
            empty_str[2] = 0x30 + value % 10;
            return empty_str;
        }
        break;
    }
    {% endif %}
    {% endfor %}
{% if platform_name == "drumlogue" %}
#ifdef PRINTHOOK
// edit the case number and change the variable type in the corresponding
// parameter entry in header.c to:
// k_unit_param_type_strings
    case k_user_unit_param_id_for_debug: {
        int index = (value + PRINTBUFSIZE - printBufIndex) % PRINTBUFSIZE;
        return printBuf[index];
        break;
    }
#endif
{% endif %}
    default:
        break;
    }
    return nullptr;
}
{% else %}
__unit_callback const char * unit_get_param_str_value(uint8_t id, int32_t value\
) {
    return nullptr;
}
{% endif %}

__unit_callback void unit_set_tempo(uint32_t tempo) {
    {% if sys_tempo is defined %}
    sys_tempo = tempo;
    sys_tempo_dirty = true;
    {% endif %}
}

{% if unit_type == "synth" %}
__unit_callback void unit_note_on(uint8_t note, uint8_t velocity) {
    {% if noteon_trig is defined %}
    noteon_trig_dirty = true;
    {% endif %}
    {% if unit_type == "synth" %}
    note_event_enqueue(&note_event_q, note, velocity);
    {% endif %}
}

__unit_callback void unit_note_off(uint8_t note) {
    {% if noteoff_trig is defined %}
    noteoff_trig_dirty = true;
    {% endif %}
    {% if unit_type == "synth" %}
    note_event_enqueue(&note_event_q, note, 0);
    {% endif %}
}

__unit_callback void unit_gate_on(uint8_t velocity) {
    unit_note_on(params[k_user_unit_param_note], velocity);
}

__unit_callback void unit_gate_off() {
    unit_note_off(params[k_user_unit_param_note]);
}

__unit_callback void unit_all_note_off() {
    for(int i = 0 ; i < 128 ; i++) {
        unit_note_off(i);
    }
}

__unit_callback void unit_pitch_bend(uint16_t bend) {
    bendin = bend;
    bendin_dirty = true;
}

__unit_callback void unit_channel_pressure(uint8_t pressure) {
    touchin = pressure;
    touchin_dirty = true;
}

__unit_callback void unit_aftertouch(uint8_t note, uint8_t aftertouch) {
}

__unit_callback void unit_load_preset(uint8_t idx) {
}

__unit_callback uint8_t unit_get_preset_index() {
    return 0;
}

__unit_callback const char * unit_get_preset_name(uint8_t idx) {
    return nullptr;
}

{% endif %}
