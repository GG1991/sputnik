from paraview.simple import *
import numpy as np
import scipy as sc
import scipy
import os
import vtk
import sys
import vtk2numpy as vn

#----------------------------------------------------------------# 

fx = np.zeros( (14,2) )

for i in range(2,16):
    
    print "case ", i
    fin   = 'run_3/rve_'+`i`+'/macro_t_1.pvtu'
    PVTU  = XMLPartitionedUnstructuredGridReader(FileName=fin)
    KEYs  = PVTU.GetPointDataInformation().keys()
    KEYs  = PVTU.GetCellDataInformation().keys()
    
    PlotOverLine1 = PlotOverLine(Input=PVTU, guiName="PlotOverLine1", Source="High Resolution Line Source")
    PlotOverLine1.Source.Point1 = [30.0, 00.0, 0.0]
    PlotOverLine1.Source.Point2 = [30.0, 30.0, 0.0]
    PlotOverLine1.Source.Resolution = 1000
    PlotOverLine1.UpdatePipeline()
    
    key    = "stress"
    stress = vn.getPtsData( PlotOverLine1, key )
    
    key    = "displ"
    Displ  = vn.getPtsData( PlotOverLine1, key )
    
    key    = "arc_length"
    leng   = vn.getPtsData( PlotOverLine1, key )
    
    disp0 = Displ[0,0]
    
    fx[i-2,0] = i
    fx[i-2,1] = np.trapz(stress[:,0],leng)
	
np.savetxt("run_3/run_3.dat", fx)
