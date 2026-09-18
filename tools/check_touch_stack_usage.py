"""Static budget for new touch paths, not a measured hardware high-water mark."""
import re
from pathlib import Path
from stack_frames import frame_entries, require_frame

ROOT=Path(__file__).resolve().parents[1]


def main():
    entries=frame_entries(ROOT/'.pio/build/wt32-sc01-plus')
    def frame(file, name):
        return require_frame(entries,'src/'+file+'.cpp.su',name)
    ui='ui/ui_service'
    stack=int(re.search(r'ui_stack_bytes\s*=\s*(\d+)U',(ROOT/'src/application/application.cpp').read_text())[1])
    owner=frame('application/application','Application::ui_task_entry(')+frame(ui,'UiService::run_once(')
    draw=frame(ui,'UiService::draw_tags(')+frame(ui,'TagFlow::screen(')+256
    # Includes LVGL event/render nesting or JSON recursion, plus 1 KiB for
    # construction/navigation helpers. Compiler frames alone are insufficient.
    refresh=owner+frame(ui,'UiService::refresh_current(')+frame(ui,'UiService::refresh_tags(')+max(draw+2048,frame(ui,'TagFlow::consume(')+3072)+1024
    keyboard=owner+frame(ui,'UiService::tag_action(')+frame('ui/touch_input_screen','TouchInputScreen::open(')+frame('ui/touch_input_screen','TouchInputScreen::draw(')+4096
    reserve=stack-max(refresh,keyboard)
    print(f'UI stack={stack}; new refresh estimate={refresh}; keyboard/event estimate={keyboard}; reserve={reserve}; required=4096')
    assert reserve>=4096,'New touchscreen path below 4 KiB static reserve'


if __name__=='__main__':main()
