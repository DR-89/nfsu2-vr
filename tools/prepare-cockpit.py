#!/usr/bin/env python3
"""Pack one Tripo cabin as a bounded NCPK asset without procedural trim.

Input is a decoded GLB. Decode/simplify the source first with glTF Transform:
  gltf-transform simplify source.glb simplified.glb --ratio .55 --error .003
Dependencies: numpy, pillow, trimesh (see cockpit-requirements.txt).
"""
import argparse
import json
import struct
import hashlib
from pathlib import Path
import numpy as np
from PIL import Image
import trimesh
from trimesh.visual.material import SimpleMaterial
from trimesh.visual.texture import TextureVisuals


def atlas_pack(materials, uvsets):
    """Shelf-pack bounded textures, keeping one 4096-square GPU atlas."""
    images=[]; unique={}; image_ids=[]
    for m in materials:
        image=m.image.convert('RGBA')
        key=(image.size,hashlib.sha256(image.tobytes()).digest())
        if key not in unique:
            unique[key]=len(images); images.append(image)
        image_ids.append(unique[key])
    side=4096; pad=4
    for scale in (.99,.75,.65,.55,.45,.35):
        sizes=[(max(8,int(i.width*scale)),max(8,int(i.height*scale))) for i in images]
        order=sorted(range(len(images)),key=lambda i:sizes[i][1],reverse=True)
        placement={}; x=y=row=0
        for i in order:
            w,h=sizes[i]
            if x+w+2*pad>side: y+=row; x=row=0
            if y+h+2*pad>side: break
            placement[i]=(x+pad,y+pad,w,h); x+=w+2*pad; row=max(row,h+2*pad)
        if len(placement)==len(images): break
    assert len(placement)==len(images), 'atlas overflow'
    atlas=Image.new('RGBA',(side,side),(25,27,30,255)); mapped=[]
    for i,image in enumerate(images):
        x,y,w,h=placement[i]; image=image.resize((w,h),Image.Resampling.LANCZOS)
        atlas.paste(image,(x,y))
        # Extruded borders keep linear texture filtering inside each material.
        atlas.paste(image.crop((0,0,1,h)).resize((pad,h)),(x-pad,y))
        atlas.paste(image.crop((w-1,0,w,h)).resize((pad,h)),(x+w,y))
        atlas.paste(image.crop((0,0,w,1)).resize((w,pad)),(x,y-pad))
        atlas.paste(image.crop((0,h-1,w,h)).resize((w,pad)),(x,y+h))
    for i,uv in zip(image_ids,uvsets):
        x,y,w,h=placement[i]
        q=np.clip(uv,0,1).copy()
        q[:,0]=(x+.5+q[:,0]*(w-1))/side
        q[:,1]=1-(y+.5+(1-q[:,1])*(h-1))/side
        mapped.append(q)
    return SimpleMaterial(image=atlas),np.concatenate(mapped)


def prepare(source, destination, preview, config):
    scene = trimesh.load(source, force='scene')
    parts, wheel, wheel_names = [], [], []
    # All geometry and materials come from the one Tripo model. This transform
    # only fits its driver eye and size to the seated OpenXR coordinate system.
    transform = np.asarray(config['transform'], dtype=float)
    assert transform.shape == (4, 4) and np.isfinite(transform).all()
    wheel_nodes = config['wheel_nodes']
    rim_node = config['rim_node']
    assert rim_node in wheel_nodes and len(set(wheel_nodes)) == len(wheel_nodes)
    assert set(wheel_nodes).issubset(scene.graph.nodes_geometry)
    adjustments = config.get('node_adjustments', {})
    assert set(adjustments).issubset(scene.graph.nodes_geometry)
    excluded = set(config.get('exclude_nodes', []))
    mirrored = set(config.get('mirror_nodes', []))
    assert (excluded | mirrored).issubset(scene.graph.nodes_geometry)
    assert not (excluded & (mirrored | set(wheel_nodes)))
    mirror = np.diag([-1., 1., 1., 1.])
    cleaned = {}
    for node in scene.graph.nodes_geometry:
        if node in excluded:
            continue
        matrix, geometry = scene.graph[node]
        mesh = scene.geometry[geometry].copy()
        if node in config.get('glass_remnant_nodes', []):
            # Tripo left glass slivers attached to the dark pillars. Identify
            # neutral bright glass on the side-window plane, preserving the
            # textured black trim. This removes faces, not material detail.
            source_mesh = mesh.copy()
            source_mesh.apply_transform(matrix)
            tex = np.asarray(mesh.visual.material.baseColorTexture.convert('RGB'))
            uv = mesh.visual.uv[mesh.faces]
            samples = np.stack([uv.mean(axis=1), *(uv[:,i]*.6+uv.mean(axis=1)*.4 for i in range(3))],axis=1)
            x = np.clip((samples[:,:,0]*(tex.shape[1]-1)).astype(int),0,tex.shape[1]-1)
            y = np.clip(((1-samples[:,:,1])*(tex.shape[0]-1)).astype(int),0,tex.shape[0]-1)
            rgb = tex[y,x].astype(float)
            glass = ((rgb.min(axis=2)>80) & (np.ptp(rgb,axis=2)<28)).sum(axis=1)>=2
            glass &= (np.abs(source_mesh.face_normals[:,0])>.7) & (source_mesh.triangles_center[:,1]>-.025)
            cleaned[node] = int(glass.sum())
            mesh = mesh.submesh([~glass], append=True)
        # A reference may lift the roof to reveal the cabin. Refit that existing
        # Tripo part in source coordinates; never synthesize replacement trim.
        adjustment = np.asarray(adjustments.get(node, np.eye(4)), dtype=float)
        assert adjustment.shape == (4, 4) and np.isfinite(adjustment).all()
        mesh.apply_transform(transform @ adjustment @ matrix)
        if node in wheel_nodes:
            wheel.append(mesh)
            wheel_names.append(node)
            if node == rim_node:
                # Fit the rim plane, not the hub, to preserve the tilted shaft.
                _, basis = np.linalg.eigh(np.cov(mesh.vertices.T))
                axis = basis[:, 0]
                if axis[2] < 0: axis = -axis
                local = mesh.vertices @ basis
                pivot = ((local.min(axis=0)+local.max(axis=0))/2) @ basis.T
        else:
            parts.append(mesh)
            if node in mirrored:
                other = scene.geometry[geometry].copy()
                if node in cleaned:
                    other = other.submesh([~glass], append=True)
                other.apply_transform(transform @ mirror @ adjustment @ matrix)
                parts.append(other)
    assert len(wheel) == len(wheel_nodes), 'Missing steering geometry'
    assert parts
    for i, node in enumerate(wheel_names):
        cut = config.get('wheel_stationary_cut_m', {}).get(node)
        if cut is not None:
            m = wheel[i]
            moving = (m.triangles_center-pivot) @ axis >= cut
            assert moving.any() and not moving.all(), 'Expected both hub and column'
            wheel[i] = m.submesh([moving], append=True)
            parts.append(m.submesh([~moving], append=True))

    wheel_first = sum(m.faces.size for m in parts)
    wheel_count = sum(m.faces.size for m in wheel)
    fixed_vertices = sum(len(m.vertices) for m in parts)
    parts.extend(wheel)
    materials = [m.visual.material.to_simple() if hasattr(m.visual.material,'to_simple')
                 else m.visual.material for m in parts]
    assert all(m.image is not None for m in materials), 'Every cabin part must be textured'
    uvsets = [m.visual.uv for m in parts]
    material, uv = atlas_pack(materials, uvsets)
    vertices = np.concatenate([m.vertices for m in parts])
    normals = np.concatenate([m.vertex_normals for m in parts])
    faces = np.concatenate([m.faces + offset for m, offset in
                           zip(parts, np.cumsum([0] + [len(m.vertices) for m in parts[:-1]]))])
    # Trimesh's UV origin is bottom-left; Vulkan atlas rows start at the top.
    gpu_uv = uv.copy(); gpu_uv[:, 1] = 1-gpu_uv[:, 1]
    vertex_data = np.concatenate((vertices, normals, gpu_uv), axis=1).astype('<f4')
    assert len(vertices) <= 250000 and faces.size <= 750000
    assert np.isfinite(vertex_data).all()
    assert np.abs(vertices).max() < 5
    assert (faces >= 0).all() and faces.max() < len(vertices)
    atlas = material.image.convert('RGBA')
    w, h = atlas.size
    header = struct.pack('<4s7I6f2I', b'NCPK', 2, len(vertices), faces.size, w, h,
                         wheel_first, wheel_count, *pivot, *axis, 0, 0)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(header + vertex_data.tobytes() + faces.astype('<u4').tobytes() + atlas.tobytes())
    result = trimesh.Scene()
    result.add_geometry(trimesh.Trimesh(vertices=vertices[:fixed_vertices],
        faces=faces[:wheel_first//3], vertex_normals=normals[:fixed_vertices],
        visual=TextureVisuals(uv=uv[:fixed_vertices],material=material),process=False),node_name='cabin')
    result.add_geometry(trimesh.Trimesh(vertices=vertices[fixed_vertices:]-pivot,
        faces=faces[wheel_first//3:]-fixed_vertices, vertex_normals=normals[fixed_vertices:],
        visual=TextureVisuals(uv=uv[fixed_vertices:],material=material),process=False),
        node_name='steering_wheel',transform=trimesh.transformations.translation_matrix(pivot))
    result.export(preview)
    metadata = {'id': config['id'], 'source_project': config['source_project'],
                'source_result': config['source_result'],
                'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'geometry_origin': 'One Tripo cabin; existing trim refitted/mirrored, no procedural cabin geometry',
                'excluded_nodes': sorted(excluded), 'mirrored_nodes': sorted(mirrored),
                'node_adjustments': adjustments,
                'removed_glass_remnant_faces': cleaned,
                'palette': config['palette'],
                'triangles':len(faces), 'vertices':len(vertices), 'atlas':[w,h],
                'coordinates':'OpenXR, meters, driver eye at origin',
                'game_seat_forward_left_up_m':config['game_seat_forward_left_up_m'],
                'steering':{'nodes':wheel_nodes, 'pivot':pivot.tolist(),
                            'axis':axis.tolist(), 'first_index':wheel_first, 'index_count':wheel_count,
                            'max_degrees':config['steering_max_degrees'], 'input':'player 1 left-stick X delivered to the game'},
                'includes':config['includes']}
    destination.with_suffix('.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(json.dumps(metadata))


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--preview',type=Path,required=True)
    parser.add_argument('--config',type=Path,required=True)
    args=parser.parse_args()
    prepare(args.source,args.output,args.preview,json.loads(args.config.read_text()))
