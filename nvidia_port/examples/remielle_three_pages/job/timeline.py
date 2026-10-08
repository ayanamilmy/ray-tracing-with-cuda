"""Original detail-page actions, original durations; page dwell times are authored."""
import json
from pathlib import Path
import numpy as np
D=Path(__file__).resolve().parent
ROWS=json.loads((D.parent/'data/camera.json').read_text())['avatarShowSettings'][0]
PAGES={name:next(r['AvatarShowSetting']['AvatarCameraSetting'] for r in ROWS if r['Tips']==tip) for name,tip in [('Detail','基础'),('Skill_01','技能'),('Equipment','装备')]}
SEGMENTS=[(0,1,'Detail','Detail',None,0),(1,4,'DtoS1','Skill_01','Detail',1),(4,4.75,'Skill_01','Skill_01',None,0),(4.75,7.75,'S1toE','Equipment','Skill_01',.75),(7.75,8.5,'Equipment','Equipment',None,0),(8.5,11.5,'EtoD','Detail','Equipment',.75),(11.5,12,'Detail','Detail',None,0)]
DURATION=12.;FPS=120;FRAMES=1440
def smooth(x):x=np.clip(x,0,1);return x*x*(3-2*x)
def rotation(x,y):
 x,y=np.radians([x,y]);rx=np.array([[1,0,0],[0,np.cos(x),-np.sin(x)],[0,np.sin(x),np.cos(x)]]);ry=np.array([[np.cos(y),0,np.sin(y)],[0,1,0],[-np.sin(y),0,np.cos(y)]])
 return ry@rx
def page_camera(page):
 c=PAGES[page];origin=(rotation(c['CameraPith'],c['CameraYaw'])@np.array(c['CameraOffset']))*[1,1,-1]+[0,0,0]
 target=np.array(c['LookAtPointOffset'])*[1,1,-1]+[0,0,0]
 # Authored safety framing for this aspect ratio; raw game values remain in camera JSON.
 distance=np.linalg.norm(origin-target)
 if distance<1.75:origin=target+(origin-target)*(1.75/distance)
 return origin,target,((c['CameraRoll']+180)%360-180),c['CameraFOV']
def state(t):
 start,end,clip,page,previous,held=next(s for s in SEGMENTS if s[0]<=t<s[1]);at=t-start
 blend=(previous,(held+at)*60,min(at/.2,1)) if previous and at<.2 else None
 origin,target,roll,fov=page_camera(page)
 if previous:
  a,b,r,f=page_camera(previous);w=smooth(at/.6600000262260437)
  origin=a*(1-w)+origin*w;target=b*(1-w)+target*w;roll=r+w*((roll-r+180)%360-180);fov=f*(1-w)+fov*w
 back=(origin-target)/np.linalg.norm(origin-target);right=np.cross([0,1,0],back);right/=np.linalg.norm(right);up=np.cross(back,right)
 a=np.radians(roll);rr=right*np.cos(a)+up*np.sin(a);uu=-right*np.sin(a)+up*np.cos(a)
 # An explicit physical directional emitter follows the camera, not shader-only lighting.
 directions={k:(page_camera(k)[0]-page_camera(k)[1])/np.linalg.norm(page_camera(k)[0]-page_camera(k)[1]) for k in PAGES}
 light=directions[page]+np.array([0,.4,0])
 if previous:light=(directions[previous]+np.array([0,.4,0]))*(1-w)+light*w
 light/=np.linalg.norm(light)
 return dict(clip=clip,source_frame=at*60,blend=blend,page=page,origin=origin,target=target,right=rr,up=uu,back=back,roll=roll,fov=fov,light=light)
def unroll(v,s):
 """Metal's existing CLI has no roll: rigidly rotate the entire scene equivalently."""
 b=s['back'];angle=-np.radians(s['roll']);k=np.array([[0,-b[2],b[1]],[b[2],0,-b[0]],[-b[1],b[0],0]])
 r=np.eye(3)+np.sin(angle)*k+(1-np.cos(angle))*(k@k)
 v['positions']=(v['positions']-s['origin'])@r.T+s['origin'];v['normals']=v['normals']@r.T
 v['attributes'][:,:,0,:3]=v['attributes'][:,:,0,:3]@r.T
 v['head_forward']=r@v['head_forward'];v['head_right']=r@v['head_right']
 return r@s['light']
