"""Backend-local static linear-blend skinning from FBX cluster weights.
Pose angles/weapon placement are authored, not recovered animation clips.
Cluster Transform is used as stored: Link @ Transform gives the mesh bind matrix.
"""
import numpy as np
from fbx_static import scalar

AXIS_CONVERSION=np.array([[1.,0,0],[0,0,1],[0,-1,0]])

def rotation_matrix(degrees):
    x,y,z=np.radians(degrees)
    rx=np.array([[1,0,0],[0,np.cos(x),-np.sin(x)],[0,np.sin(x),np.cos(x)]])
    ry=np.array([[np.cos(y),0,np.sin(y)],[0,1,0],[-np.sin(y),0,np.cos(y)]])
    rz=np.array([[np.cos(z),-np.sin(z),0],[np.sin(z),np.cos(z),0],[0,0,1]])
    return rz@ry@rx

class SkinPose:
    def __init__(self,objects,connections,document):
        self.byid={n['props'][0]:n for n in objects};self.connections=connections;self.document=document
        self.parent={};self.bind={};self.clusters={};self.names={}
        for n in objects:
            if n['name']=='Model': self.names[n['props'][1].split('\0')[0]]=n['props'][0]
        for c in connections:
            if c[0]=='OO' and c[1] in self.byid and c[2] in self.byid:
                if self.byid[c[1]]['name']==self.byid[c[2]]['name']=='Model':self.parent[c[1]]=c[2]
        for n in objects:
            if n['name']!='Deformer' or n['props'][2]!='Cluster':continue
            bone=[c[1] for c in connections if c[2]==n['props'][0] and c[1] in self.byid and self.byid[c[1]]['name']=='Model']
            if len(bone)!=1:raise ValueError('Expected one bone per skin cluster')
            link=scalar(n,'TransformLink').reshape(4,4).T.copy();transform=scalar(n,'Transform').reshape(4,4).T.copy()
            self.bind[bone[0]]=link
            skins=[c[2] for c in connections if c[1]==n['props'][0]]
            geometries=[c[2] for c in connections if c[1] in skins and c[2] in self.byid and self.byid[c[2]]['name']=='Geometry']
            for geometry in geometries:self.clusters.setdefault(geometry,[]).append((bone[0],n,link,transform))
        self.root_delta=np.eye(4)
        if document.get('root_rotation'):
            pivot_name=document.get('root_pivot_bone','Bip001 Pelvis')
            pivot=self.bind[self.names[pivot_name]][:3,3]
            self.root_delta[:3,:3]=rotation_matrix(document['root_rotation'])
            self.root_delta[:3,3]=pivot-self.root_delta[:3,:3]@pivot
        # The character mesh bind matrix defines the conversion to renderer coordinates.
        body=next(n for n in objects if n['name']=='Geometry' and n['props'][1].split('\0')[0]=='Zhenzhen_Body')
        _,_,link,transform=self.clusters[body['props'][0]][0]
        conversion=np.eye(4);conversion[:3,:3]=AXIS_CONVERSION
        self.render_from_world=conversion@np.linalg.inv(link@transform)
        self.render_from_world[:3,3]+=[0,.037648990750312805,0]
        self.edits={};self.deltas={}
        for name,angles in document.get('bones',{}).items():
            if name not in self.names or self.names[name] not in self.bind:raise ValueError('Missing bound pose bone: '+name)
            if len(angles)!=3 or not np.isfinite(angles).all():raise ValueError('Invalid authored rotation')
            self.edits[self.names[name]]=rotation_matrix(angles)
        for name,angles in document.get('local_bones',{}).items():
            if name not in self.names or self.names[name] not in self.bind:raise ValueError('Missing bound local pose bone: '+name)
            if len(angles)!=3 or not np.isfinite(angles).all():raise ValueError('Invalid local authored rotation')
            bone=self.names[name];basis=self.bind[bone][:3,:3]
            self.edits[bone]=basis@rotation_matrix(angles)@np.linalg.inv(basis)
        # Explicit absolute aims are expressed in renderer axes, then converted
        # back to the parent frame. Geometry still uses the original cluster weights.
        for name,aim in document.get('aim_bones',{}).items():
            bone=self.names[name];parent=self.parent.get(bone)
            inherited=self.delta(parent) if parent is not None else self.root_delta
            z=np.array(aim['z'],dtype=float);z/=np.linalg.norm(z)
            x=np.array(aim['x'],dtype=float);x-=np.dot(x,z)*z;x/=np.linalg.norm(x)
            y=np.cross(z,x);target=np.stack([x,y,z],axis=1)
            world_target=np.linalg.inv(self.render_from_world[:3,:3])@target
            self.edits[bone]=np.linalg.inv(inherited[:3,:3])@world_target@np.linalg.inv(self.bind[bone][:3,:3])
            self.deltas.clear()
        for name in document.get('gaze',{}).get('bones',[]):
            bone=self.names[name];parent=self.parent.get(bone)
            inherited=self.delta(parent) if parent is not None else self.root_delta
            frame=self.bone_frame(name)
            target=np.array(document['gaze']['target'])-frame[:3,3];target/=np.linalg.norm(target)
            forward=self.render_from_world[:3,:3]@inherited[:3,:3]@np.array([0.,0.,1.]);forward/=np.linalg.norm(forward)
            axis=np.cross(forward,target);cosine=np.clip(np.dot(forward,target),-1,1)
            skew=np.array([[0,-axis[2],axis[1]],[axis[2],0,-axis[0]],[-axis[1],axis[0],0]])
            swing=np.eye(3)+skew+skew@skew/max(1+cosine,1e-9)
            render_parent=self.render_from_world[:3,:3]@inherited[:3,:3]
            self.edits[bone]=np.linalg.inv(render_parent)@swing@render_parent
            self.deltas.clear()
        self.report={'method':'Static linear-blend FBX cluster skinning; inherited world-axis rotations', 'meshes':{},'authored_bones':document.get('bones',{}),'authored_local_bones':document.get('local_bones',{})}
    def delta(self,bone):
        if bone in self.deltas:return self.deltas[bone]
        parent=self.parent.get(bone);inherited=self.delta(parent) if parent is not None else self.root_delta
        change=np.eye(4)
        if bone in self.edits:
            pivot=self.bind[bone][:3,3];change[:3,:3]=self.edits[bone];change[:3,3]=pivot-change[:3,:3]@pivot
        self.deltas[bone]=inherited@change
        return self.deltas[bone]
    def bone_frame(self,name):
        bone=self.names[name]
        return self.render_from_world@self.delta(bone)@self.bind[bone]
    def attachment_frame(self,w):
        frame=self.bone_frame(w['bone'])
        target=frame[:3,:3]@np.array(w['offset'])+frame[:3,3]
        # The JSON selects a proper signed permutation of the actual hand axes.
        mapping=w.get('axes',['-x','z','y'])
        columns=[]
        for axis in mapping:
            columns.append(frame[:3,'xyz'.index(axis[-1])]*(-1 if axis.startswith('-') else 1))
        r=np.stack(columns,axis=1)
        if not np.allclose(r.T@r,np.eye(3),atol=1e-5) or np.linalg.det(r)<.99:raise ValueError('Invalid attachment axes')
        return target,r
    def deform(self,geometry,vertices):
        name=geometry['props'][1].split('\0')[0]
        if name==self.document.get('weapon',{}).get('mesh'):
            w=self.document['weapon'];pivot=np.array(w['pivot'])
            if 'bone' in w:target,r=self.attachment_frame(w)
            else:target=np.array(w['target']);r=rotation_matrix(w.get('rotation',[0,0,0]))
            transformed=((vertices@AXIS_CONVERSION.T-pivot)*w.get('scale',1))@r.T+target-(np.array([0,.037648990750312805,0]) if 'bone' in w else 0)
            self.report['meshes'][name]={'rigid_authored_weapon':w,'grip_target_render':target.tolist(),'attachment_basis':r.tolist()}
            return transformed@AXIS_CONVERSION,np.repeat((AXIS_CONVERSION.T@r@AXIS_CONVERSION)[None],len(vertices),axis=0)
        clusters=self.clusters.get(geometry['props'][0],[])
        matrices=np.zeros((len(vertices),4,4));weights=np.zeros(len(vertices));identity_error=0.
        for bone,n,link,transform in clusters:
            mesh=link@transform
            deformation=np.linalg.inv(mesh)@self.delta(bone)@link@transform
            identity_error=max(identity_error,float(np.abs(np.linalg.inv(mesh)@link@transform-np.eye(4)).max()))
            indices=scalar(n,'Indexes');values=scalar(n,'Weights')
            if indices is None and values is None:continue  # FBX may carry empty clusters.
            if indices is None or values is None or len(indices)!=len(values):raise ValueError('Incomplete cluster weights')
            np.add.at(matrices,indices,values[:,None,None]*deformation);np.add.at(weights,indices,values)
        active=weights>1e-10
        matrices[active]/=weights[active,None,None];matrices[~active]=np.eye(4)
        result=np.einsum('nij,nj->ni',matrices,np.c_[vertices,np.ones(len(vertices))])[:,:3]
        linear=matrices[:,:3,:3]
        if not np.isfinite(result).all() or np.any(np.abs(np.linalg.det(linear))<1e-6):raise ValueError('Invalid skin deformation')
        # Linear transforms also transport the original tangent frame.
        self.report['meshes'][name]={'clusters':len(clusters),'unweighted_vertices':int((~active).sum()),'max_bind_identity_error':identity_error,'max_displacement':float(np.linalg.norm(result-vertices,axis=1).max()),'weight_min':float(weights[active].min()) if active.any() else 0,'weight_max':float(weights.max())}
        return result,linear
