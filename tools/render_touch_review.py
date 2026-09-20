"""Closest-available WT32 layout fixtures, using production geometry.

These are not LVGL framebuffers or proof of physical touch/color fidelity.
The renderer checks bounds, action sizes, and text fit at 480 x 320.
"""
from pathlib import Path
import argparse,re,json,hashlib
from check_public_privacy import assert_public_text
from PIL import Image,ImageDraw,ImageFont

ROOT=Path(__file__).resolve().parents[1]
BOXES={name:tuple(map(int,coords.split(','))) for name,coords in re.findall(r'Box (\w+)\{([\d, ]+)\}',(ROOT/'src/ui/product_layout.hpp').read_text())}
for name,(x,y,w,h) in BOXES.items():
    assert 0<=x<x+w<=480 and 0<=y<y+h<=320,name
BG='#101416';SURFACE='#344248';TEXT='#f3f5f3';MUTED='#cbd5e1';MINT='#72dfbe'
def font(size):
    for path in ['C:/Windows/Fonts/segoeui.ttf','/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf']:
        if Path(path).exists():return ImageFont.truetype(path,size)
    return ImageFont.load_default(size=size)

def render(scene,out):
    im=Image.new('RGB',(480,320),BG);d=ImageDraw.Draw(im)
    def label(box,text,size=16,color=TEXT):
        assert_public_text(text)
        x,y,w,h=BOXES[box];f=font(size)
        for line_no,line in enumerate(text.split('\n')):
            while d.textbbox((0,0),line,font=f)[2]>w:line=line[:-2]+'…'
            assert (line_no+1)*(size+4)<=h+4,(scene,box,text)
            d.text((x+(w-d.textbbox((0,0),line,font=f)[2])/2 if scene=='empty' else x,y+line_no*(size+4)),line,font=f,fill=color)
    def button(box,text,primary=False):
        assert_public_text(text)
        x,y,w,h=BOXES[box];assert h>=44 and w>=44
        d.rounded_rectangle((x,y,x+w-1,y+h-1),8,fill=MINT if primary else SURFACE)
        f=font(16);bounds=d.multiline_textbbox((0,0),text,font=f,align='center');assert bounds[2]<=w-8 and bounds[3]<=h-4,(box,text)
        d.multiline_text((x+(w-bounds[2])/2,y+(h-bounds[3])/2-2),text,font=f,fill='#112c25' if primary else TEXT,align='center')
    page={'empty':0,'home':0,'weigh':1,'assign':2,'tag':3,'settings':4}[scene]
    for i,title in enumerate(['Home','Weigh','Assign','Tag','Settings']):
        assert_public_text(title)
        d.rectangle((i*96,272,(i+1)*96-1,319),fill='#303a3f' if i==page else '#191f22')
        d.text((i*96+(96-d.textbbox((0,0),title,font=font(16))[2])/2,285),title,font=font(16),fill=MINT if i==page else MUTED)
    if scene in ['home','empty']:
        label('home_state','SUNLU' if scene=='home' else 'OpenTag Station',color=MINT)
        label('home_material','PLA+ 2.0 Black' if scene=='home' else 'Place a spool',20)
        label('home_identity','Spool #28  |  Tag valid' if scene=='home' else 'Set a tagged spool on the station',color=MUTED)
        if scene=='home':label('home_weight','712 g remaining',32)
        x,y,w,h=BOXES['home_art' if scene=='home' else 'empty_art'];d.ellipse((x,y,x+w,y+h),fill='#191f22',outline='#788583',width=6);d.ellipse((x+w/2-9,y+h/2-9,x+w/2+9,y+h/2+9),fill=BG,outline='#788583',width=2)
        button('home_weigh','WEIGH',True);button('home_assign','ASSIGN');button('home_tag','MANAGE TAG')
    elif scene=='weigh':
        label('heading','PLA+ 2.0 Black',20);label('gross','842',32);label('gross_unit','GROSS WEIGHT (g)',color=MUTED);label('quality','STABLE',color=MINT)
        label('receipt','Empty spool  130 g\nFilament  712 g\nSpoolman  720 g\nDifference  -8 g')
        button('weigh','WEIGH AGAIN',True);button('update','Update Spoolman');label('feedback','Measurement differs by 8 g',14,MUTED)
    elif scene=='assign':
        label('printer_name',"Prusa XL",20);label('printer_spool','PLA+ 2.0 Black · Spool #28',color=MUTED)
        for i in range(1,6):button('tool'+str(i),'T'+str(i)+'\n'+({2:'Spool #27',4:'Spool #31'}.get(i,'Empty')))
        label('feedback','Select a toolhead to review assignment',14,MUTED)
    elif scene=='tag':
        label('heading','Manage tag',20);label('tag_detail','OpenPrintTag recognized\nPLA+ 2.0 Black\nLinked to Spoolman #28')
        button('tag_update','UPDATE TAG',True);button('tag_clear','CLEAR / REUSE');button('tag_confirm','Review before writing')
    else:
        label('heading','Display brightness',20);x,y,w,h=BOXES['brightness'];d.rounded_rectangle((x,y+18,x+w,y+26),4,fill=SURFACE);d.rounded_rectangle((x,y+18,x+w*.8,y+26),4,fill=MINT);d.ellipse((x+w*.8-10,y+12,x+w*.8+10,y+32),fill=TEXT)
        button('policy','Auto-update OFF');button('calibration','Calibrate scale');button('about','About / Advanced');label('wifi','Wi-Fi connected\n192.0.2.42',16,MUTED)
    im.save(out/f'wt32-{scene}.png')

def model_scene(name,scene,out):
    im=Image.new('RGB',(480,320),BG);d=ImageDraw.Draw(im)
    keyboard='input' in scene
    def text(box,value,size=16):
        assert_public_text(value)
        x,y,w,h=box;f=font(size);lines=[]
        for paragraph in value.split('\n'):
            line=''
            for word in paragraph.split(' '):
                candidate=(line+' '+word).strip()
                if d.textbbox((0,0),candidate,font=f)[2]>w and line:lines.append(line);line=word
                else:line=candidate
            lines.append(line)
        capacity=max(1,h//(size+3))
        for i,line in enumerate(lines[:capacity]):
            while len(line)>1 and d.textbbox((0,0),line,font=f)[2]>w:line=line[:-2]+'…'
            d.text((x,y+i*(size+3)),line,font=f,fill=TEXT)
        if len(lines)>capacity:d.text((x+w-14,y+h-20),'↕',font=font(16),fill=MINT)
    title_width=266 if keyboard and not scene.get('numeric') else 300 if any(b['box'][1]==4 for b in scene['buttons']) else 448
    text((8 if keyboard else 16,4 if scene.get('numeric') else 8,title_width,28),scene['title'],20)
    if keyboard:
        y,h=(30,32) if scene.get('numeric') else (52,44)
        d.rounded_rectangle((8,y,472,y+h),4,fill=SURFACE)
        text((14,y+3,452,h-3),scene['input'],20)
    else:text(scene['body_box'],scene['body'])
    boxes=[]
    for b in scene['buttons']:
        x,y,w,h=b['box'];assert 4<=x<x+w<=476 and 4<=y<y+h<=316 and w>=44 and h>=44,(name,b)
        for bx,by,bw,bh in boxes:assert x+w<=bx or bx+bw<=x or y+h<=by or by+bh<=y,(name,b)
        boxes.append((x,y,w,h));primary=b['text'] in ['SEARCH','DONE','WRITE TAG','CREATE SPOOL','REVIEW TAG WRITE']
        d.rounded_rectangle((x,y,x+w-1,y+h-1),6,fill='#147d73' if primary else SURFACE,outline='#52645f')
        value=b['text'];size=20 if keyboard and len(value)==1 else 16
        text((x+6,y+max(3,(h-(size+3)*min(2,value.count('\n')+1))//2),w-12,h-6),value,size)
    im.save(out/f'wt32-{name}.png')

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    scenes=['empty','home','weigh','assign','settings']
    for scene in scenes:render(scene,args.output)
    models=json.loads((ROOT/'.pio/touch-flow-fixtures.json').read_text(encoding='utf-8'))
    for name,scene in models.items():model_scene(name,scene,args.output)
    scenes+=list(models)
    images={f'wt32-{scene}.png':hashlib.sha256((args.output/f'wt32-{scene}.png').read_bytes()).hexdigest() for scene in scenes}
    (args.output/'touch-provenance.json').write_text(json.dumps({'privacy_checked_before_capture':True,'images':images},indent=2)+'\n',encoding='utf-8')
    (args.output/'WT32-READ-ME.txt').write_text(__doc__+'\nShared production layout: src/ui/product_layout.hpp\n',encoding='utf-8')
    print(f'{len(images)} WT32 fixtures: production model bounds and 44 px action checks passed')
if __name__=='__main__':main()
