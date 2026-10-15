#!/usr/bin/env python3
"""Fit the generated Tripo right glove and bake relaxed/grasped poses offline.

NGHV v1: eight LE uint32 header words (magic, version, vertices per pose,
texture width/height, three reserved zeros), two nonindexed PNUV float8 meshes,
then RGBA8. Left hand mirrors this same authored right glove at runtime.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import numpy as np
import trimesh
from PIL import Image

def prepare(source, output, preview):
    scene = trimesh.load(source, force='scene')
    assert len(scene.geometry) == 1
    mesh = list(scene.geometry.values())[0].copy()
    assert 0 < len(mesh.faces) <= 20000
    # Source +Y is fingertips, -X is the blue back, -Z is thumb. Canonical
    # right hand: +Y fingertips, +Z back, thumb on -X. Scale in meters.
    raw = mesh.vertices.copy()
    p = np.column_stack(((raw[:,2]-.035)*.22, (raw[:,1]+.15)*.22,
                         -(raw[:,0]+.035)*.13))
    mesh.vertices = p
    image = mesh.visual.material.baseColorTexture.convert('RGBA').resize((1024,1024),Image.Resampling.LANCZOS)
    image.putalpha(255)
    uv = np.clip(mesh.visual.uv,0,1)
    closed = mesh.copy()
    q = p.copy()
    # Bend each finger around a rim-sized cylinder. Weight thumb separately;
    # preserve its web/palm continuously rather than cutting disconnected parts.
    thumb = np.clip((-p[:,0]-.037)/.023,0,1)*np.clip((.095-p[:,1])/.030,0,1)
    thumb = thumb*thumb*(3-2*thumb)
    pivot = np.array([-.035,-.005,-.002]); local = p-pivot
    rz = trimesh.transformations.rotation_matrix(np.deg2rad(-40),[0,0,1])[:3,:3]
    ry = trimesh.transformations.rotation_matrix(np.deg2rad(-35),[0,1,0])[:3,:3]
    thumb_pose = local@(ry@rz).T+pivot
    length = np.maximum(p[:,1]-.035,0)
    radius = .030
    angle = np.minimum(length/radius,2.55)
    tail = np.maximum(length-radius*2.55,0)
    # Rotate the local thickness with the bend, preserving a continuous skin.
    bend = p.copy()
    bend[:,1] = .035+radius*np.sin(angle)+tail*np.cos(angle)+p[:,2]*np.sin(angle)
    bend[:,2] = radius*(np.cos(angle)-1)-tail*np.sin(angle)+p[:,2]*np.cos(angle)
    bend[length==0] = p[length==0]
    q = bend*(1-thumb[:,None])+thumb_pose*thumb[:,None]
    closed.vertices = q
    material=trimesh.visual.material.PBRMaterial(baseColorTexture=image,metallicFactor=0,roughnessFactor=.8,alphaMode='BLEND',baseColorFactor=[255,255,255,190])
    for m in (mesh,closed): m.visual=trimesh.visual.TextureVisuals(uv=uv.copy(),material=material)
    count = len(mesh.faces)*3
    arrays=[]
    gpu_uv=uv.copy();gpu_uv[:,1]=1-gpu_uv[:,1] # Vulkan rows start at the top.
    for m in (mesh,closed):
        ix=m.faces.ravel()
        vertices=np.column_stack((m.vertices,m.vertex_normals,gpu_uv))[ix].astype('<f4')
        assert np.isfinite(vertices).all() and np.abs(vertices[:,:3]).max()<.3
        arrays.append(vertices.tobytes())
    blob=struct.pack('<4s7I',b'NGHV',1,count,1024,1024,0,0,0)+b''.join(arrays)+image.tobytes()
    output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(blob)
    preview.parent.mkdir(parents=True,exist_ok=True)
    mesh.export(preview.with_name(preview.stem+'-open.glb'))
    closed.export(preview.with_name(preview.stem+'-grip.glb'))
    meta={'source_task':'5e8ee832-072e-42f9-8edf-a4566c71d52b','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
          'sha256':hashlib.sha256(blob).hexdigest(),'triangles_per_hand':len(mesh.faces),'texture':[1024,1024],
          'canonical_axes':'right glove, +Y fingertips, +Z back, thumb -X','pose_bounds_m':[mesh.bounds.tolist(),closed.bounds.tolist()],
          'preparation':'Source geometry/texture retained; metric fit, thumb rotation and continuous finger curl baked offline.'}
    output.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(meta,indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('source',type=Path);parser.add_argument('output',type=Path);parser.add_argument('--preview',type=Path,required=True)
    args=parser.parse_args();prepare(args.source,args.output,args.preview)
