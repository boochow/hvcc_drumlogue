import os
import shutil
import time
import jinja2
import re
import json
import math
from abc import ABC, abstractmethod

from typing import Dict, Optional, Tuple

from hvcc.types.compiler import CompilerResp, ExternInfo, Generator, CompilerNotif, CompilerMsg
from hvcc.interpreters.pd2hv.NotificationEnum import NotificationEnum
from hvcc.types.meta import Meta
from hvcc.core.hv2ir.HeavyLangObject import HeavyLangObject

def ndigits(x):
    return round(math.log10(abs(x)) + 0.5) if abs(x) > 1 else 1

def scale(n):
    if n == 0:
        return 3
    return 3 - int(math.log10(abs(n)))

def set_min_value(dic, key, value):
    if key not in dic:
        dic[key] = value
    else:
        dic[key] = min(value, dic[key])
    return dic[key] == value

def render_from_template(template_file, rendered_file, context):
    common_templates_dir = os.path.join(os.path.dirname(__file__), context['platform_name'], "common", "templates")
    unit_templates_dir = os.path.join(os.path.dirname(__file__), context['platform_name'], context['unit_type'], "templates")
    templates_dir = [
        d for d in (unit_templates_dir, common_templates_dir)
        if os.path.isdir(d)
    ]
    loader = jinja2.FileSystemLoader(templates_dir)
    env = jinja2.Environment(loader=loader, trim_blocks=True, lstrip_blocks=True)
    rendered = env.get_template(template_file).render(**context)
    with open(rendered_file, 'w') as f:
        f.write(rendered)

class classproperty(property):
    def __get__(self, obj, owner):
        return self.fget(owner)

class LogueSDKV2Generator(Generator, ABC):
    PLATFORM_NAME: str = "drumlogue"
    FIXED_PARAMS: Tuple[str, ...] = ()
    BUILTIN_PARAMS: Tuple[str, ...] = ()
    CONDITIONAL_PARAMS: Tuple[str, ...] = ()
    UNIT_NUM_INPUT: int = 2
    UNIT_NUM_OUTPUT: int = -1
    MAX_SDRAM_SIZE: int = 0
    SDRAM_ALLOC_THRESHOLD: int = 256
    MAX_UNIT_SIZE: int = 0
    MSG_POOL_SIZE_KB: int = 1
    INPUT_QUEUE_SIZE_KB: int = 1
    OUTPUT_QUEUE_SIZE_KB: int = 0
    MSG_POOL_ON_SRAM = False
    MAX_DIGITS = 5
    MAX_PARAM_COUNT = 24
    MAX_ENCODER_STEP = 100

    @abstractmethod
    def unit_type(): return None

    @classproperty
    def fixed_params(cls): return cls.FIXED_PARAMS

    @classproperty
    def builtin_params(cls): return cls.BUILTIN_PARAMS

    @classproperty
    def conditional_params(cls): return cls.CONDITIONAL_PARAMS

    @classproperty
    def fixed_params_f(cls): return tuple(f"{n}_f" for n in cls.FIXED_PARAMS)

    @classproperty
    def conditional_params_f(cls): return tuple(f"{n}_f" for n in cls.CONDITIONAL_PARAMS)

    @classproperty
    def max_param_num(cls): return cls.MAX_PARAM_COUNT - len(cls.FIXED_PARAMS) - len(cls.CONDITIONAL_PARAMS)

    @classmethod
    def process_builtin_param(cls, param, context: dict):
        p_name, p_rcv = param
        context[p_name] = {}
        context[p_name]['hash'] = p_rcv.hash
    
    @classmethod
    def compile(
            cls,
            c_src_dir: str,
            out_dir: str,
            externs: ExternInfo,
            patch_name: Optional[str] = None,
            patch_meta: Meta = Meta(),
            num_input_channels: int = 0,
            num_output_channels: int = 0,
            copyright: Optional[str] = None,
            verbose: Optional[bool] = False
    ) -> CompilerResp:
        begin_time = time.time()
        print(f"--> Invoking {cls.__name__}")

        out_dir = os.path.join(out_dir, "logue_unit")

        try:
            # check num of channels
            if num_input_channels > cls.UNIT_NUM_INPUT:
                print(f"Warning: {num_input_channels} input channels(ignored)")
            if num_output_channels > cls.UNIT_NUM_OUTPUT:
                raise Exception(f"{cls.unit_type().upper()} units support only {cls.UNIT_NUM_OUTPUT}ch output.")

            # ensure that the output directory does not exist
            out_dir = os.path.abspath(out_dir)
            if os.path.exists(out_dir):
                shutil.rmtree(out_dir)

            # copy over static files
            common_static_dir = os.path.join(os.path.dirname(__file__), cls.PLATFORM_NAME, "common", "static")
            unit_static_dir = os.path.join(os.path.dirname(__file__), cls.PLATFORM_NAME, cls.unit_type(), "static")
            os.makedirs(out_dir, exist_ok=False)
            for static_dir in [common_static_dir, unit_static_dir]:
                if os.path.isdir(static_dir):
                    for filename in os.listdir(static_dir):
                        src_path = os.path.join(static_dir, filename)
                        dst_path = os.path.join(out_dir, filename)
                        shutil.copy2(src_path, dst_path)

            # copy C files
            for file in os.listdir(c_src_dir):
                src_file = os.path.join(c_src_dir, file)
                if os.path.isfile(src_file):
                    dest_file = os.path.join(out_dir, file)
                    shutil.copy2(src_file, dest_file)

            # values for rendering templates
            context = {
                'class_name': cls.__name__,
                'platform_name': cls.PLATFORM_NAME,
                'unit_type': cls.unit_type(),
                'max_sdram_size': cls.MAX_SDRAM_SIZE,
                'sdram_alloc_threshold': cls.SDRAM_ALLOC_THRESHOLD,
                'max_unit_size': cls.MAX_UNIT_SIZE,
                'patch_name': patch_name,
                'msg_pool_size_kb': cls.MSG_POOL_SIZE_KB,
                'input_queue_size_kb': cls.INPUT_QUEUE_SIZE_KB,
                'output_queue_size_kb': cls.OUTPUT_QUEUE_SIZE_KB,
                'num_input_channels' : num_input_channels,
                'num_output_channels' : num_output_channels,
                'num_fixed_param': len(cls.fixed_params)
            }

            # list of source files
            heavy_files_c = [
                f for f in os.listdir(c_src_dir)
                if os.path.isfile(os.path.join(out_dir, f)) and f.endswith('.c')
            ]
            context['heavy_files_c'] =  " ".join(heavy_files_c)

            heavy_files_cpp = [
                f for f in os.listdir(c_src_dir)
                if os.path.isfile(os.path.join(out_dir, f)) and f.endswith('.cpp')
            ]
            context['heavy_files_cpp'] = ' '.join(heavy_files_cpp)

            conditional_params = []
            other_params = []

            # key: table name, value: parameter names related to the table
            soundloader = {}
            # parameters for the size of a pcm table become built-in param
            pcm_builtin_params = []
            pcm_index_params = {}
            pcm_bank_params = {}
            for table in externs.tables:
                t_name, t_tbl = table
                if t_name.endswith('_s'):
                    pcm_params = {
                        'disp_name': t_name[:-2],
                        # input params as built-in params
                        'size_param': t_name[:-2] + "_size",
                        'selected_param': t_name[:-2] + "_selected",
                        'indexMenu_param': t_name[:-2] + "_indexMenu",
                        'bankMenu_param': t_name[:-2] + "_bankMenu",
                        'size_param_hash': "0x{0:X}".format(HeavyLangObject.get_hash(t_name[:-2] + "_size")),
                        # output params
                        'set_param': t_name[:-2] + "_set",
                        'set_param_hash': "0x{0:X}".format(HeavyLangObject.get_hash(t_name[:-2] + "_set")),
                    }
                    pcm_builtin_params.append(pcm_params['size_param'])
                    pcm_builtin_params.append(pcm_params['selected_param'])
                    pcm_index_params[pcm_params['indexMenu_param']] = t_name
                    pcm_bank_params[pcm_params['bankMenu_param']] = t_name
                    soundloader[t_name] = pcm_params
            context['soundloader'] = soundloader

            # special external input parameters
            for param in externs.parameters.inParam:
                p_name, p_rcv = param
                p_attr = p_rcv.attributes
                p_range = p_attr['max'] - p_attr['min']
                if p_name in cls.builtin_params:
                    cls.process_builtin_param(param, context)
                elif p_name in pcm_builtin_params:
                    cls.process_builtin_param(param, context)
                elif p_name in cls.conditional_params:
                    conditional_params.append(param)
                    context[p_name] = {'name' : p_name}
                    context['p_'+p_name+'hash'] = p_rcv.hash
                    if p_attr['min'] == 0. and p_attr['max'] == 1.0:
                        context[p_name]['range'] = 1023
                        context[p_name]['min'] = 0
                        context[p_name]['max'] = 1023
                        context[p_name]['default'] = 512
                    else:
                        context[p_name]['range'] = p_range
                        context[p_name]['min'] = p_attr['min']
                        context[p_name]['max'] = p_attr['max']
                        context[p_name]['default'] = p_attr['default']
                elif p_name in cls.fixed_params_f:
                    context[p_name[:-2]] = {'name' : p_name}
                    context[p_name[:-2]]['range_f'] = p_range
                    context[p_name[:-2]]['min_f'] = p_attr['min']
                    context[p_name[:-2]]['max_f'] = p_attr['max']
                    context[p_name[:-2]]['default'] = p_attr['default']
                    context[p_name[:-2]]['hash'] = p_rcv.hash
                    if p_name == 'mix_f':
                        context[p_name[:-2]]['min'] = -100
                        context[p_name[:-2]]['max'] = 100
                    else:
                        context[p_name[:-2]]['min'] = 0
                        context[p_name[:-2]]['max'] = 1023
                else:
                    other_params.append(param)

            # parameter meta info for NTS-3's parameter assignment
            p_meta = {}
            # parse parameter names
            pattern = re.compile(r'^(?:_(\d*)([xXyYzZ]?)([aAbBcCdDlLrR]?)_)?(.*)$')
            for param in other_params:
                p_name, p_rcv = param
                p_attr = p_rcv.attributes
                match = pattern.fullmatch(p_rcv.display)
                digits, device, curve, body = match.groups()
                if digits is None:
                    digits = ''
                if device is None:
                    device = ''
                if curve is None:
                    curve = ''

                # parameter index
                if digits != '':
                    p_index = int(digits) - 1
                else:
                    p_index = None

                # parameter assignment to input devices
                p_assign = 'k_genericfx_param_assign_none'
                if device != '':
                    c = device.lower()
                    if c == 'x':
                        p_assign = 'k_genericfx_param_assign_x'
                    elif c == 'y':
                        p_assign = 'k_genericfx_param_assign_y'
                    elif c == 'z':
                        p_assign = 'k_genericfx_param_assign_depth'

                # parameter mapping curve type
                p_curve = 'k_genericfx_curve_linear'
                p_polarity = 'k_genericfx_curve_unipolar'
                if curve != '':
                    if curve.isupper():
                        p_polarity = 'k_genericfx_curve_bipolar'
                    c = curve.lower()
                    if c == 'a':
                        p_curve = 'k_genericfx_curve_exp'
                    elif c == 'b':
                        p_curve = 'k_genericfx_curve_linear'
                    elif c == 'c':
                        p_curve = 'k_genericfx_curve_log'
                    elif c == 'd':
                        p_curve = 'k_genericfx_curve_toggle'
                    elif c == 'r':
                        p_curve = 'k_genericfx_curve_minclip'
                    elif c == 'l':
                        p_curve = 'k_genericfx_curve_maxclip'

                # parameter type as a variable type
                if body in pcm_index_params.keys():
                    p_disp_name = body[:-10] # "_indexMenu"
                    p_param_type = '*pcm_index*'
                    p_attr['type'] = '*pcm_index*'
                elif body in pcm_bank_params.keys():
                    p_disp_name = "Bk:" + body[:-9][:4] # "_bankMenu"
                    p_param_type = '*pcm_bank*'
                    p_attr['type'] = '*pcm_bank*'
                elif body.endswith("_f"):
                    p_disp_name = body[:-2]
                    p_param_type = 'float'
                else:
                    p_disp_name = body
                    p_param_type = 'int'

                # show fractional part if parameter type is float
                p_max = p_attr['max']
                p_min = p_attr['min']
                if p_param_type == 'float':
                    if cls.PLATFORM_NAME == 'drumlogue':
                        # assume using special float-to-string formatter
                        p_disp_frac = 10 # this is used for the format flag
                        p_disp_max = cls.MAX_ENCODER_STEP
                        p_disp_min = 0
                        p_disp_default = (p_attr['default'] - p_min) * cls.MAX_ENCODER_STEP / (p_max - p_min)
                    else:
                        # assume using decimal mode for frac
                        num_digits = max(ndigits(p_max), ndigits(p_min))
                        p_disp_frac = cls.MAX_DIGITS - num_digits
                        p_disp_max = p_max * pow(10, p_disp_frac)
                        p_disp_min = p_min * pow(10, p_disp_frac)
                        p_disp_default = p_attr['default'] * pow(10, p_disp_frac)
                else:
                    p_disp_frac = 0
                    p_disp_max = max(-32768, min(32767, int(p_max)))
                    p_disp_min = max(-32768, int(p_min))
                    p_disp_default = max(-32768, min(32767, p_attr['default']))

                p_meta[p_name] = {
                    'index' : p_index,
                    'assign' : p_assign,
                    'curve' : p_curve,
                    'polarity' : p_polarity,
                    'type' : p_param_type,
                    'disp_name' : p_disp_name,
                    'disp_frac' : p_disp_frac,
                    'disp_max' : p_disp_max,
                    'disp_min' : p_disp_min,
                    'disp_default' : p_disp_default,
                }
                if body in pcm_index_params.keys():
                    p_meta[p_name]['table'] = pcm_index_params[body]
                elif body in pcm_bank_params.keys():
                    p_meta[p_name]['table'] = pcm_bank_params[body]

            # unit parameters (ordered)
            unit_params = [None] * cls.max_param_num

            # first, place parameters with index numbers
            for param in other_params:
                p_name, p_rcv = param
                index = p_meta[p_name]['index']
                if index is not None:
                    if not (0 <= index < cls.max_param_num):
                        raise IndexError(f"Index {index} is out of range.")
                    if unit_params[index] is not None:
                        print(f'Warning: parameter slot {index + 1} is duplicated ({unit_params[index][0]}, {p_name})')
                        p_meta[p_name]['index'] = None
                    else:
                        unit_params[index] = param

            # place parameters without index numbers
            for param in other_params:
                p_name, p_rcv = param
                index = p_meta[p_name]['index']
                if index is None:
                    for i, value in enumerate(unit_params):
                        if value is None:
                            unit_params[i] = param
                            break
                    else:
                        print("Warning: too many parameters")
            
            # store all parameter information to the context
            context['param'] = {}
            for i in range(cls.max_param_num):
                if unit_params[i] is None:
                    continue
                # prefix (parameter number)
                p_key = f'param_id{i+1}'
                p_name, p_rcv = unit_params[i]
                p_attr = p_rcv.attributes

                context['param'][p_key] = {'name' : p_name}
                context['param'][p_key]['hash'] = p_rcv.hash
                context['param'][p_key]['max'] = p_attr['max']
                context['param'][p_key]['min'] = p_attr['min']
                context['param'][p_key]['default'] = p_attr['default']
                if 'type' in p_attr:
                    type = p_attr['type'].lower()
                elif 'type' in p_meta:
                    type = p_meta['type'].lower()
                else:
                    type = ""
                # types below add a unit string after values
                if type == 'percent':
                    format = 'k_unit_param_type_percent'
                elif type == 'db':
                    format = 'k_unit_param_type_db'
                elif type == 'cents':
                    format = 'k_unit_param_type_cents'
                elif type == 'hertz':
                    format = 'k_unit_param_type_hertz'
                elif type == 'khertz':
                    format = 'k_unit_param_type_khertz'
                elif type == 'ms':
                    format = 'k_unit_param_type_msec'
                elif type == 'sec':
                    format = 'k_unit_param_type_sec'
                # types below add "+/-" before values
                elif type == 'semi':
                    format = 'k_unit_param_type_semi'
                elif type == 'oct':
                    format = 'k_unit_param_type_oct'
                # other types with their own formats
                elif type == 'drywet':
                    format = 'k_unit_param_type_drywet'
                elif type == 'pan':
                    format = 'k_unit_param_type_pan'
                elif type == 'spread':
                    format = 'k_unit_param_type_spread'
                elif type == 'onoff':
                    format = 'k_unit_param_type_onoff'
                elif type == 'midinote':
                    format = 'k_unit_param_type_midi_note'
                elif type == '*platform*':
                    format = 'k_unit_param_type_strings'
                else:
                    format = 'k_unit_param_type_none'

                context['param'][p_key]['format'] = format

                context['param'][p_key].update(p_meta[p_name])

            # outParams
            context['out_param'] = {}
            for param in externs.parameters.outParam:
                p_name, p_rcv = param
                p_attr = p_rcv.attributes
                context['out_param'][p_name] = {
                    'hash': p_rcv.hash,
                    'max': p_attr['max'],
                    'min': p_attr['min'],
                    'default': p_attr['default'],
                    'type': p_attr['type']
                    }

            # find the total number of parameters
            for i in range(cls.max_param_num - 1, -1, -1):
                if unit_params[i] is not None:
                    num_param = i + 1
                    break
            else:
                num_param = 0
            context['num_param'] = num_param
            context['num_conditional_param'] = len(conditional_params)

            # store tables into a dcitionary (context)
            context['table'] = {}
            for table in externs.tables:
                t_name, t_tbl = table
                t_disp_name = t_name[:-2]
                context['table'][t_name] = {'name' : t_name, 'disp_name' : t_disp_name}
                context['table'][t_name]['hash'] = t_tbl.hash
                if t_name.endswith('_r'):
                    context['table'][t_name]['type'] = 'random'
                elif t_name.endswith('_s'):
                    context['table'][t_name]['type'] = 'sample'
                else:
                    context['table'][t_name]['type'] = 'none'

            # verbose
            if verbose:
                print(f"input channels:{num_input_channels}")
                print(f"output channels:{num_output_channels}")
                print(f"parameters: {externs.parameters}")
                print(f"events: {externs.events}")
                print(f"midi: {externs.midi}")
                print(f"tables: {externs.tables}")
                print(f"context: {json.dumps(context, indent=2, ensure_ascii=False)}")

            # estimate required heap memory
            if cls.PLATFORM_NAME != 'drumlogue':
                render_from_template('Makefile.testmem',
                                     os.path.join(out_dir, "Makefile.testmem"),
                                     context)
                render_from_template('testmem.cpp',
                                     os.path.join(out_dir, "testmem.cpp"),
                                     context)
            
            # render files
            render_from_template('config.mk',
                                 os.path.join(out_dir, "config.mk"),
                                 context)
            render_from_template('logue_heavy.cpp',
                                 os.path.join(out_dir, "logue_heavy.cpp"),
                                 context)
            render_from_template('header.c',
                                 os.path.join(out_dir, "header.c"),
                                 context)

            # add definitions to HvUtils.h
            if cls.PLATFORM_NAME != 'drumlogue':
                hvutils_src_path = os.path.join(c_src_dir, "HvUtils.h")
                hvutils_dst_path = os.path.join(out_dir, "HvUtils.h")
                with open(hvutils_src_path, 'r', encoding='utf-8') as f:
                    src_lines = f.readlines()
    
                dst_lines = []
                for line in src_lines:
                    if "// Assert" in line:
                        dst_lines.append('#include "logue_mem_hv.h"')
                        dst_lines.append("\n\n")
                    elif "// Atomics" in line:
                        dst_lines.append('#include "logue_math_hv.h"')
                        dst_lines.append("\n\n")
                    dst_lines.append(line)

                with open(hvutils_dst_path, 'w', encoding='utf-8') as f:
                    f.writelines(dst_lines)

            # add definitions to Heavy_heavy.cpp to keep the object on SRAM
            if cls.PLATFORM_NAME != 'drumlogue':
                mainclass_src_path = os.path.join(c_src_dir, f"Heavy_{patch_name}.cpp")
                mainclass_dst_path = os.path.join(out_dir, f"Heavy_{patch_name}.cpp")
                with open(mainclass_src_path, 'r', encoding='utf-8') as f:
                    src_lines = f.readlines()

                dst_lines = []
                for line in src_lines:
                    if "#include <new>" in line:
                        dst_lines.append('#include "logue_mem_hv_sram.h"')
                        dst_lines.append("\n\n")
                    dst_lines.append(line)

                with open(mainclass_dst_path, 'w', encoding='utf-8') as f:
                    f.writelines(dst_lines)

            # add definitions to HvMessagePool.c (workaround for delay&reverb)
            if cls.MSG_POOL_ON_SRAM:
                hvmessagepool_src_path = os.path.join(c_src_dir, "HvMessagePool.c")
                hvmessagepool_dst_path = os.path.join(out_dir, "HvMessagePool.c")
                with open(hvmessagepool_src_path, 'r', encoding='utf-8') as f:
                    src_lines = f.readlines()

                dst_lines = []
                for line in src_lines:
                    if "#if HV_APPLE" in line:
                        dst_lines.append('#include "logue_mem_hv_sram.h"')
                        dst_lines.append("\n\n")
                    dst_lines.append(line)

                with open(hvmessagepool_dst_path, 'w', encoding='utf-8') as f:
                    f.writelines(dst_lines)

            # done
            end_time = time.time()

            return CompilerResp(
                stage='LogueSDKV2Generator',  # module name
                compile_time=end_time - begin_time,
                in_dir=c_src_dir,
                out_dir=out_dir
            )

        except Exception as e:
            return CompilerResp(
                stage=f"{cls.__name__}",
                notifs=CompilerNotif(
                    has_error=True,
                    exception=e,
                    warnings=[],
                    errors=[CompilerMsg(
                        enum=NotificationEnum.ERROR_EXCEPTION,
                        message=str(e)
                    )]
                ),
                in_dir=c_src_dir,
                out_dir=out_dir,
                compile_time=time.time() - begin_time
            )
