"""Exercise the production Java adapters against firmware and view stand-ins."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BooxAdapterTests(unittest.TestCase):
    def test_firmware_calls_regions_restoration_and_fallback(self):
        java_home = os.environ.get('JAVA_HOME')
        javac = str(Path(java_home) / 'bin/javac') if java_home else shutil.which('javac')
        java = str(Path(java_home) / 'bin/java') if java_home else shutil.which('java')
        if not javac or not java:
            self.skipTest('JDK is required for the Java firmware adapter regression')
        sources = {
            'android/os/Build.java': '''package android.os;
public class Build { public static String MANUFACTURER="ONYX", BRAND="ONYX"; }''',
            'android/util/Log.java': '''package android.util;
public class Log { public static int w(String a,String b,Throwable t){return 0;}
public static int d(String a,String b){return 0;} }''',
            'org/lsposed/hiddenapibypass/HiddenApiBypass.java': '''package org.lsposed.hiddenapibypass;
public class HiddenApiBypass { public static int calls;
public static boolean addHiddenApiExemptions(String... names) {
 if(names.length<1 || names.length>2 || !names[0].equals("Landroid/onyx/")) throw new AssertionError();
 if(names.length==2 && !names[1].equals("Landroid/view/Surface;->addEpdc([I)V")) throw new AssertionError();
 ++calls; return true;
} }''',
            'android/onyx/hardware/DeviceController.java': '''package android.onyx.hardware;
public class DeviceController {
 public static int brightness=28,warmth=0,max=32,flags=-1,writes;
 public static boolean fail;
 public static boolean checkCTM(){return true;}
 public static int getMaxLightValue(int type){return max;}
 public static int getLightValue(int type){ if(fail)throw new IllegalStateException(); return type==7?brightness:warmth; }
 public static void setLightValue(int type,int value,int f){flags=f;++writes;if(type==7)brightness=value;else warmth=value;}
}''',
            'android/onyx/ViewUpdateHelper.java': '''package android.onyx;
public class ViewUpdateHelper {public static int UI_GU_MODE=12, UI_REGAL_MODE=16, UI_GC_MODE=198;}''',
            'android/graphics/Rect.java': '''package android.graphics;
public class Rect {
 public int left,top,right,bottom;
 public Rect(int l,int t,int r,int b){left=l;top=t;right=r;bottom=b;}
 public Rect(Rect r){this(r.left,r.top,r.right,r.bottom);}
 public void union(Rect r){left=Math.min(left,r.left);top=Math.min(top,r.top);right=Math.max(right,r.right);bottom=Math.max(bottom,r.bottom);}
 public void offset(int x,int y){left+=x;right+=x;top+=y;bottom+=y;}
 public int width(){return right-left;}public int height(){return bottom-top;}
 public boolean intersect(int l,int t,int r,int b){
  if(left>=r||right<=l||top>=b||bottom<=t)return false;
  left=Math.max(left,l);top=Math.max(top,t);right=Math.min(right,r);bottom=Math.min(bottom,b);return true;
 }
}''',
            'android/view/View.java': '''package android.view;
public class View {
 public int mode=5,calls,x,y,w,h,lastMode;
 public boolean shown=true,attached=true;
 public boolean isShown(){return shown;}public boolean isAttachedToWindow(){return attached;}
 public int getWidth(){return 400;}public int getHeight(){return 600;}
 public void getLocationOnScreen(int[] p){p[0]=20;p[1]=100;}
 public int getDefaultUpdateMode(){return mode;}public void setDefaultUpdateMode(int value){mode=value;}
 public void refreshScreen(int a,int b,int c,int d,int m){++calls;x=a;y=b;w=c;h=d;lastMode=m;}
}''',
            'android/view/ViewGroup.java': '''package android.view;
public class ViewGroup extends View {
 public View[] children; public ViewGroup(View... views){children=views;}
 public int getChildCount(){return children.length;}public View getChildAt(int i){return children[i];}
}''',
            'android/view/Surface.java': '''package android.view;
public class Surface {
 public int calls;public int[] entry;public boolean valid=true,fail;
 public boolean isValid(){return valid;}
 public void addEpdc(int[] value){if(fail)throw new IllegalStateException();++calls;entry=value.clone();}
 public int[] queue(){int[] value=entry;entry=null;return value;}
}''',
            'android/view/SurfaceHolder.java': '''package android.view;
public class SurfaceHolder {public Surface surface=new Surface();public Surface getSurface(){return surface;}}''',
            'android/view/SurfaceView.java': '''package android.view;
public class SurfaceView extends View {private SurfaceHolder holder=new SurfaceHolder();public SurfaceHolder getHolder(){return holder;}}''',
            'org/qtproject/qt/android/QtSurface.java': '''package org.qtproject.qt.android;
public class QtSurface extends android.view.SurfaceView {}''',
            'org/kobalt/android/AdapterCheck.java': '''package org.kobalt.android;
import android.graphics.Rect;
import android.onyx.hardware.DeviceController;
import org.qtproject.qt.android.QtSurface;
public class AdapterCheck {
 static void check(boolean ok){if(!ok)throw new AssertionError();}
 public static void main(String[] args){
  BooxDevice light=new BooxDevice(); check(light.available());check(DeviceController.writes==0);
  check(light.light(1,7,0)==32);check(light.light(2,7,16)==16);
  check(DeviceController.flags==0 && DeviceController.warmth==0);
  check(light.light(2,6,8)==8 && DeviceController.brightness==16);
  check(light.light(2,7,33)==-1 && DeviceController.writes==2);
  check(light.light(2,4,1)==-1);
  DeviceController.fail=true;check(light.light(0,7,0)==-1);check(!light.available());
  DeviceController.fail=false;DeviceController.max=0;check(!new BooxDevice().available());DeviceController.max=32;
  QtSurface view=new QtSurface();android.view.Surface surface=view.getHolder().getSurface();
  android.view.ViewGroup root=new android.view.ViewGroup(view);
  BooxRefresh refresh=new BooxRefresh();check(refresh.available());
  check(!refresh.prepare(new int[]{30,120,300,500,1},false));
  refresh.publish(root,true);
  check(refresh.prepare(new int[]{30,120,300,500,1},false));
  int[] packet=surface.queue();
  check(java.util.Arrays.equals(packet,new int[]{10,20,280,400,16}));
  check(view.mode==5 && view.calls==0 && surface.entry==null);
  check(refresh.prepare(new int[]{0,0,900,900,1},true));
  check(java.util.Arrays.equals(surface.queue(),new int[]{0,0,400,600,198}));
  check(refresh.prepare(new int[]{30,120,300,500,0},false));
  check(surface.queue()[4]==12);
  check(refresh.prepare(new int[]{30,120,90,150,0,300,500,390,580,1},false));
  check(java.util.Arrays.equals(surface.queue(),new int[]{10,20,70,50,12,280,400,370,480,16}));
  check(refresh.prepare(new int[]{0,0,50,120,0,900,900,950,950,1},false));
  check(java.util.Arrays.equals(surface.queue(),new int[]{0,0,30,20,12}));
  check(!refresh.prepare(new int[]{1,2,3},false));
  check(!refresh.prepare(new int[]{30,120,90,150,3},false));
  check(!refresh.prepare(new int[]{},false));
  check(refresh.prepare(new int[]{30,120,90,150,0},true));
  check(java.util.Arrays.equals(surface.queue(),new int[]{0,0,400,600,198}));
  int calls=surface.calls;
  refresh.publish(root,false);check(!refresh.prepare(new int[]{30,120,300,500,1},true));
  check(surface.calls==calls);refresh.publish(root,true);
  surface.valid=false;check(!refresh.prepare(new int[]{30,120,300,500,1},false));
  check(surface.calls==calls);surface.valid=true;
  check(refresh.prepare(new int[]{30,120,300,500,1},true));check(surface.queue()[4]==198);
  refresh.cancel();check(!refresh.prepare(new int[]{30,120,300,500,1},false));
  view.attached=false;refresh.publish(root,true);check(!refresh.prepare(new int[]{30,120,300,500,1},false));
  view.attached=true;view.shown=false;refresh.publish(root,true);check(!refresh.prepare(new int[]{30,120,300,500,1},false));
  view.shown=true;QtSurface other=new QtSurface();
  refresh.publish(new android.view.ViewGroup(view,other),true);
  check(!refresh.available() && view.mode==5 && other.mode==5);
  refresh=new BooxRefresh();refresh.publish(root,true);surface.fail=true;
  check(!refresh.prepare(new int[]{30,120,300,500,1},false));check(!refresh.available());
  check(new BooxDevice().available());
  android.os.Build.MANUFACTURER="Other";android.os.Build.BRAND="Other";
  int compat=org.lsposed.hiddenapibypass.HiddenApiBypass.calls;
  check(!new BooxDevice().available());check(!new BooxRefresh().available());
  check(org.lsposed.hiddenapibypass.HiddenApiBypass.calls==compat);
 }
}''',
        }
        scratch = ROOT / 'build'
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temporary:
            folder = Path(temporary)
            for name, content in sources.items():
                path = folder / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            for name in ('BooxDevice.java', 'BooxRefresh.java'):
                shutil.copyfile(ROOT / 'android/app/src/org/kobalt/android' / name,
                                folder / 'org/kobalt/android' / name)
            result = subprocess.run([javac, '-d', str(folder), *map(str, folder.rglob('*.java'))],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([java, '-cp', str(folder), 'org.kobalt.android.AdapterCheck'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
