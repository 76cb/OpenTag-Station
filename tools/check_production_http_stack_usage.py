#!/usr/bin/env python3
"""Audit ordinary production HTTP paths and retain the existing OTA bound.

Ordinary routes include a further 2 KiB allowance for dispatch, JSON/library
leaves and logging before requiring 4 KiB headroom. The unchanged OTA path
retains its historical 1088-byte allowance and 2 KiB reserve. These estimates
are regression guards; target high-water measurements remain release evidence.
"""
import argparse
import pathlib
import re
from check_diagnostic_stack_usage import frame_entries, require_frame

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=pathlib.Path,
                        default=ROOT / '.pio/build/wt32-sc01-plus')
    entries = frame_entries(parser.parse_args().build_dir)

    def frame(file, method):
        return require_frame(entries, 'src/' + file + '.cpp.su', method)

    server = 'web/local_web_server'
    router = 'web/api_router'
    context = 'web/application_api_context'
    header = (ROOT / 'src/web/local_web_server.hpp').read_text()
    stack = int(re.search(r'http_task_stack_bytes\s*=\s*(\d+)U', header)[1])
    entry = frame(server, 'LocalWebServer::api_handler(')
    dispatch = entry + frame(server, 'LocalWebServer::handle_api(') + frame(router, 'Router::handle(')
    parse = frame(router, 'parse_mutation(') + max(
        frame(router, 'parse_configuration_patch('),
        frame(router, 'Result<T>::success(T) [with T = opentag::web::api::Mutation]'))
    submit = frame(context, 'ApplicationApiContext::submit(') + frame(context, 'ApplicationApiContext::submit_fresh(')
    patch = frame('web/configuration_patch', 'apply_configuration_patch(') + frame('config/configuration_service', 'Configuration::validate()')
    enqueue = frame('application/configuration_worker', 'ConfigurationWorker::submit_replace(')
    snapshot = frame(context, 'ApplicationApiContext::snapshot_json(')
    ordinary = dispatch + max(parse, submit + max(patch, enqueue), snapshot) + 2048
    ota = entry + frame(server, 'LocalWebServer::handle_update_upload(') + frame(context, 'ApplicationApiContext::begin_streaming_upload(') + 1088
    print(f'Production httpd stack={stack}; ordinary routes={ordinary}; remaining={stack-ordinary}; required=4096')
    print(f'Production OTA httpd path={ota}; remaining={stack-ota}; required=2048 (existing allowance)')
    assert stack - ordinary >= 4096, 'ordinary HTTP route headroom below 4 KiB'
    assert stack - ota >= 2048, 'OTA HTTP route headroom below existing 2 KiB'


if __name__ == '__main__':
    main()
