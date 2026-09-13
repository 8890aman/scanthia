# Generates a synthetic CT series (sphere in a box, HU-ish values)
# under tests/data/ct_series/ for exercising the DICOM pipeline.
import os
import numpy as np
import pydicom
from pydicom.dataset import Dataset, FileMetaDataset
from pydicom.uid import (
    ExplicitVRLittleEndian, CTImageStorage, generate_uid)

OUT = os.path.join(os.path.dirname(__file__), "data", "ct_series")
os.makedirs(OUT, exist_ok=True)

N, NZ = 128, 48
study_uid, series_uid = generate_uid(), generate_uid()
frame_uid = generate_uid()

yy, xx = np.mgrid[:N, :N]
zz = np.mgrid[:NZ]
cx = cy = N / 2
vol = np.full((NZ, N, N), -1000.0, dtype=np.float32)          # air
body = (xx - cx) ** 2 + (yy - cy) ** 2 < (N * 0.42) ** 2       # soft tissue
vol[:, body] = 40.0
r2 = (xx - cx) ** 2 + (yy - cy) ** 2
for k in range(NZ):                                          # bone sphere
    dz = k - NZ / 2
    vol[k][r2 + dz * dz < 20 ** 2] = 1200.0
vol = vol.astype(np.int16) + 1000                            # stored values

for k in range(NZ):
    meta = FileMetaDataset()
    meta.MediaStorageSOPClassUID = CTImageStorage
    meta.MediaStorageSOPInstanceUID = generate_uid()
    meta.TransferSyntaxUID = ExplicitVRLittleEndian
    meta.ImplementationClassUID = generate_uid()

    ds = Dataset()
    ds.file_meta = meta
    ds.SOPClassUID = CTImageStorage
    ds.SOPInstanceUID = meta.MediaStorageSOPInstanceUID
    ds.Modality = "CT"
    ds.PatientName = "Test^Synthetic"
    ds.PatientID = "SYNTH001"
    ds.PatientBirthDate = "19700101"
    ds.StudyDate = "20260912"
    ds.StudyTime = "120000"
    ds.AccessionNumber = "ACC001"
    ds.StudyInstanceUID = study_uid
    ds.SeriesInstanceUID = series_uid
    ds.FrameOfReferenceUID = frame_uid
    ds.StudyDescription = "Synthetic Study"
    ds.SeriesDescription = "Synthetic CT"
    ds.SeriesNumber = 1
    ds.InstanceNumber = k + 1
    ds.ImagePositionPatient = [0.0, 0.0, float(k) * 2.5]
    ds.ImageOrientationPatient = [1, 0, 0, 0, 1, 0]
    ds.PixelSpacing = [1.0, 1.0]
    ds.SliceThickness = 2.5
    ds.SpacingBetweenSlices = 2.5
    ds.Rows = N
    ds.Columns = N
    ds.BitsAllocated = 16
    ds.BitsStored = 16
    ds.HighBit = 15
    ds.PixelRepresentation = 1
    ds.SamplesPerPixel = 1
    ds.PhotometricInterpretation = "MONOCHROME2"
    ds.RescaleIntercept = -1000
    ds.RescaleSlope = 1
    ds.RescaleType = "HU"
    ds.WindowCenter = 40
    ds.WindowWidth = 400
    ds.PixelData = vol[k].tobytes()
    pydicom.dcmwrite(os.path.join(OUT, f"slice_{k:03d}.dcm"), ds)

print(f"wrote {NZ} slices to {OUT}")
