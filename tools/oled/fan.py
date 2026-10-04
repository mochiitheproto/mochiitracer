"""The ABANICOS icon: a fan with curved blades inside a ring, drawn analytically at 4x
supersampling and thresholded to 1 bit (gen_assets makes 3 frames 30 deg apart)."""
import numpy as np, math
def fan(N=32, blades=3, R=15.0, ss=4, rot=-90, ring=True, curve=3.2, wmax=34, tip=0.85):
    S=N*ss
    yy,xx=np.mgrid[0:S,0:S]
    X=(xx+0.5)/ss - N/2; Y=(yy+0.5)/ss - N/2
    d=np.hypot(X,Y); th=np.degrees(np.arctan2(Y,X))
    m=np.zeros_like(d,bool)
    if ring: m|=(d>=R-2.0)&(d<=R)
    m|=d<=3.2
    r0, r1 = 3.0, R-3.6
    for k in range(blades):
        a0=rot+k*360/blades
        t=np.clip((d-r0)/(r1-r0),0,1)
        center=a0 + (d-r0)*curve
        # width profile: narrow at hub, widest ~70%, rounded tip
        w=8 + (wmax-8)*np.sin(np.clip(t/tip,0,1)*math.pi/2)
        tipcut = t>tip
        w=np.where(tipcut, wmax*np.sqrt(np.clip(1-((t-tip)/(1-tip))**2,0,1)), w)
        da=(th-center+540)%360-180
        m|=(np.abs(da)<=w)&(d>=r0)&(d<=r1)
    o=m.reshape(N,ss,N,ss).mean(axis=(1,3))
    return (o>0.5).astype(np.uint8)
