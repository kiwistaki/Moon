import argparse
import fileinput
import os
import subprocess
import sys

parser = argparse.ArgumentParser(description='Compile all .hlsl shaders')
parser.add_argument('--dxc', type=str, help='path to DXC executable')
args = parser.parse_args()

def findDXC():
    def isExe(path):
        return os.path.isfile(path) and os.access(path, os.X_OK)

    if args.dxc != None and isExe(args.dxc):
        return args.dxc

    exe_name = "dxc"
    if os.name == "nt":
        exe_name += ".exe"

    for exe_dir in os.environ["PATH"].split(os.pathsep):
        full_path = os.path.join(exe_dir, exe_name)
        if isExe(full_path):
            return full_path

    sys.exit("Could not find DXC executable on PATH, and was not specified with --dxc")

dxc_path = findDXC()
dir_path = os.path.dirname(os.path.realpath(__file__))
dir_path = dir_path.replace('\\', '/')
for root, dirs, files in os.walk(dir_path):
    for file in files:
        if file.endswith(".vs") or file.endswith(".ps") or file.endswith(".cs") or file.endswith(".gs") or file.endswith(".ds") or file.endswith(".hs"):
            hlsl_file = os.path.join(root, file)
            cso_out = hlsl_file + ".cso"

            profile = ''
            entry = ''
            if(hlsl_file.find('.vs') != -1):
                profile = 'vs_6_5'
                entry = 'VS'
            elif(hlsl_file.find('.ps') != -1):
                profile = 'ps_6_5'
                entry = 'PS'
            elif(hlsl_file.find('.cs') != -1):
                profile = 'cs_6_5'
                entry = 'CS'
            elif(hlsl_file.find('.gs') != -1):
                profile = 'gs_6_5'
                entry = 'GS'
            elif(hlsl_file.find('.hs') != -1):
                profile = 'hs_6_5'
                entry = 'HS'
            elif(hlsl_file.find('.ds') != -1):
                profile = 'ds_6_5'
                entry = 'DS'

            print('Compiling %s' % (hlsl_file))
            subprocess.check_output([
                dxc_path,
                '-T', profile,
                '-E', entry,
                hlsl_file,
                '-Fo', cso_out])