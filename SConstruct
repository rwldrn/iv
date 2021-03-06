# vim: fileencoding=utf-8
import platform
import sys
import os
from os.path import join, dirname, abspath
root_dir = dirname(File('SConstruct').rfile().abspath)
sys.path.append(join(root_dir, 'tools'))
from GenerateFile import TOOL_SUBST

def GetVariables():
  return Variables()

def CheckEndian(ctx):
  ctx.Message('checking endianess ... ')
  import struct
  array = struct.pack('cccc', '\x01', '\x02', '\x03', '\x04')
  i = struct.unpack('i', array)
  if i == struct.unpack('<i', array):
    ctx.Result('little')
    return 'litte'
  elif i == struct.unpack('>i', array):
    ctx.Result('big')
    return 'big'
  ctx.Result('unknown')
  return 'unknown'

def Test(context, object_files):
  test_task = context.SConscript(
    'test/SConscript',
    variant_dir=join(root_dir, 'obj', 'test'),
    duplicate=False,
    exports="context object_files"
  )
  context.AlwaysBuild(test_task)
  return test_task

def Lv5(context, object_files):
  lv5_task = context.SConscript(
    'src/lv5/SConscript',
    variant_dir=join(root_dir, 'obj', 'lv5'),
    duplicate=False,
    exports="context object_files root_dir"
  )
  return lv5_task

def Main(context, deps):
  return context.SConscript(
    'src/SConscript',
    variant_dir=join(root_dir, 'obj', 'src'),
    duplicate=False,
    exports='root_dir context deps'
  )

def Build():
  options = {}
  var = GetVariables()
  var.AddVariables(
    BoolVariable('debug', '', 0),
    BoolVariable('gprof', '', 0),
    BoolVariable('gcov', '', 0)
  )
  env = Environment(options=var, tools = ['default', TOOL_SUBST])
  env.VariantDir(join(root_dir, 'obj'), join(root_dir, 'src'), 0)

  if os.path.exists(join(root_dir, '.config')):
    env.SConscript(
      join(root_dir, '.config'),
      duplicate=False,
      exports='root_dir env options')

  option_dict = {
    '%VERSION%': '0.0.1',
    '%DEVELOPER%': 'Yusuke Suzuki',
    '%EMAIL%': 'utatane.tea@gmail.com'
  }

  if not env.GetOption('clean'):
    conf = Configure(env, custom_tests = { 'CheckEndian' : CheckEndian })
    if not conf.CheckFunc('strtod'):
      print 'strtod must be provided'
      Exit(1)
    if not conf.CheckFunc('snprintf'):
      print 'strtod must be provided'
      Exit(1)
    if not conf.CheckHeader('fpu_control.h'):
      print 'this toolchain msvc compiler'
    if options.get('noicu'):
      # use iconv
      conf.CheckLibWithHeader('iconv', 'iconv.h', 'c++')
      option_dict['%USE_ICU%'] = '0'
    else:
      conf.env.ParseConfig('icu-config --cxxflags --cppflags --ldflags')
      option_dict['%USE_ICU%'] = '1'
    conf.CheckLibWithHeader('m', 'cmath', 'c++')
    env = conf.Finish()

  if options.get('cache'):
    env.CacheDir('cache')

  if sys.byteorder == 'little':
    option_dict['%IS_LITTLE_ENDIAN%'] = '1'
  else:
    option_dict['%IS_LITTLE_ENDIAN%'] = '0'

  header = env.SubstInFile(
      join(root_dir, 'src', 'config', 'config.h'),
      join(root_dir, 'src', 'config', 'config.h.in'),
      SUBST_DICT=option_dict)

  uc = env.Command(
      join(root_dir, 'src', 'ucdata.h'),
      join(root_dir, 'tools', 'UnicodeData.txt'),
      ('python %s $SOURCE $TARGET' % join(root_dir, 'tools', 'ucdata.py')))

  if env['gprof']:
    env.Append(
      CCFLAGS=["-pg"],
      LINKFLAGS=["-pg"]
    )

  if env['gcov']:
    env.Append(
      CCFLAGS=["-coverage"],
      LINKFLAGS=["-coverage"]
    )

#  if env['debug']:
#    env.Append(
#      CCFLAGS=["-g3"]
#    )
#  else:
#    env.Append(
#      #CPPDEFINES=["NDEBUG"]
#    )

  env.Append(CCFLAGS=["-g"])
  env.Append(
    CCFLAGS=[
      "-Wall", "-Wextra", "-Werror",
      "-Wno-unused-parameter", "-Wwrite-strings"],
    CPPPATH=[join(root_dir, 'src'), join(root_dir, 'obj', 'src')],
    CPPDEFINES=[
      "_GNU_SOURCE",
      "__STDC_LIMIT_MACROS",
      "__STDC_CONSTANT_MACROS"],
    LIBPATH=["/usr/lib"])
  env['ENV']['GTEST_COLOR'] = os.environ.get('GTEST_COLOR')
  env['ENV']['HOME'] = os.environ.get('HOME')
  env.Replace(YACC='bison', YACCFLAGS='--name-prefix=yy_loaddict_')
  Help(var.GenerateHelpText(env))
  # env.ParseConfig('llvm-config all --ldflags --libs')

  (object_files, main_prog) = Main(env, [header, uc])
  test_prog = Test(env, object_files)
  env.Alias('main', [main_prog])
  test_alias = env.Alias('test', test_prog, test_prog[0].abspath)
  lv5_prog = Lv5(env, object_files)
  env.Alias('lv5', [lv5_prog])
  env.Default('lv5')

Build()
