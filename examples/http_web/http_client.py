"""Small standard-library client for the existing Funiq Trading HTTP API."""
import json
import urllib.error
import urllib.request


class FuniqClient:
    def __init__(self, url='http://127.0.0.1:18080', timeout=10):
        self.url = url.rstrip('/')
        self.timeout = timeout
        self.records = []

    def request(self, path, body=None):
        req = urllib.request.Request(self.url + path,
            data=None if body is None else json.dumps(body).encode(),
            headers={'Accept': 'application/json', 'Content-Type': 'application/json'})
        try:
            response = urllib.request.urlopen(req, timeout=self.timeout)
        except urllib.error.HTTPError as exc:
            response = exc
        with response:
            result = json.load(response)
            record = {'method': req.get_method(), 'path': path, 'body': body,
                      'status': response.status, 'response': result}
            self.records.append(record)
        if record['status'] >= 400 or result.get('code') != '0':
            raise RuntimeError(f"{req.get_method()} {path}: HTTP {record['status']}, {result.get('message')}")
        return result['data']
