import numpy as np
import json
import time
import subprocess as subp
from helpers import *


def ParseTimings(pathToTimings):
  frameId = []
  timings = dict()
  with open(pathToTimings) as f:
    lines = f.readlines()
    for line in lines:
      desc, num = line[:-1].split("\t")
      if desc == "Frame":
        frameId.append(int(num))
      else:
        if frameId[-1] > 1:
          if desc in timings.keys():
            timings[desc].append(float(num))
          else:
            timings[desc] = [float(num)]
  return timings

pathVarsMapIn = "../build/varsMapIn.json"
pathVarsIcpIn = "../build/varsIcpIn.json"
pathVarsMapGen = "../build/varsMapGenerated.json"
pathVarsIcpGen = "../build/varsIcpGenerated.json"

def SetMode(samplePoints, dirObsSelect, gradNormObsSelect):
  varsMap = json.load(open(pathVarsMapIn,"r"))
  if samplePoints:
    varsMap["vars"]["mapPanel.samplePoints"] = "1"
  else:
    varsMap["vars"]["mapPanel.samplePoints"] = "0"
  varsMap["vars"]["mapPanel.save Pc on Finish"] = "1"
  varsMap["vars"]["mapPanel.exit on Finish"] = "1"
  json.dump(varsMap,open(pathVarsMapGen,"w"),indent=4,sort_keys=True)

  varsMap = json.load(open(pathVarsIcpIn,"r"))
  if dirObsSelect:
    varsMap["vars"]["icpPanel.semObsSelect"] = "1"
  else:
    varsMap["vars"]["icpPanel.semObsSelect"] = "0"
  if gradNormObsSelect:
    varsMap["vars"]["icpPanel.sortByGradient"] = "1"
  else:
    varsMap["vars"]["icpPanel.sortByGradient"] = "0"
  json.dump(varsMap,open(pathVarsIcpGen,"w"),indent=4,sort_keys=True)
def Run(dataString, configString, outputPath,
    gtPath):
  subp.call("rm surfelMap.ply ", shell=True)
  args = ["../build/experiments/sparseFusion/sparseFusion",
      dataString, 
      configString,
      pathVarsMapGen,
      pathVarsIcpGen
      ]
  print " ".join(args)
  t0 = time.time()
  err = subp.call(" ".join(args), shell=True)
  tE = time.time()
  if err:
    print "error"

  subp.call("mkdir -p "+outputPath, shell=True)
  subp.call("echo {} > ".format(tE-t0)+outputPath+"/totalTime.csv", shell=True)
  subp.call("cp timings.txt "+outputPath, shell=True)
  subp.call("cp stats.txt "+outputPath, shell=True)
  subp.call("cp trajectory_tumFormat.csv "+outputPath, shell=True)
  subp.call("mv surfelMap.ply "+outputPath, shell=True)
  subp.call("python evaluate_ate.py trajectory_tumFormat.csv "
      +gtPath+" > " +outputPath+"/trajectoryError.csv", shell=True)
  subp.call("python evaluate_avgFrameTime.py -i timings.txt "
      +" > " +outputPath+"/avgFrameTime.csv", shell=True)
