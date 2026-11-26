"""Authenticode signing of the release executables (design/installer-signing-options.md).

OFF unless tools/release/package.py is given --sign-name or --sign-pfx: by default nothing here runs and the release
assets are exactly as before (unsigned).

How a file is signed (Windows SDK signtool, SHA-256 file digest, RFC 3161 timestamp):

    signtool sign /fd SHA256 /tr <timestamp URL> /td SHA256 (/n <name> | /f <test.pfx>) <file>

  --sign-name <name>  a certificate of the CURRENT USER's certificate store, picked by its subject name. This is how a
                      cloud key is used (Certum SimplySign: the certificate appears in the store while a SimplySign
                      session is open; the private key never leaves the provider). No secret is given to this script.
  --sign-pfx <file>   a TEST certificate file (dry runs and tests only; refused for a release-channel build).

How a signature is checked, without trusting signtool's own word:
  * WinVerifyTrust (the check Windows itself runs): 0 = valid and trusted. A test certificate is expected to end in
    CERT_E_UNTRUSTEDROOT, accepted in test mode only; a changed file gives TRUST_E_BAD_DIGEST in both modes;
  * Get-AuthenticodeSignature: the signer's subject must contain the expected name, and a timestamp must be present.

No certificate, password, token or other secret is read from or written to the repository by this module.
"""
from __future__ import annotations
import ctypes, json, os, shutil, subprocess, sys, time
from pathlib import Path

DEFAULT_TIMESTAMP_URL = 'http://time.certum.pl'   # Certum's RFC 3161 service (the signing certificate's own CA)
TRUSTED, UNTRUSTED_ROOT, BAD_DIGEST, NO_SIGNATURE = 0, 0x800B0109, 0x80096010, 0x800B0100
PE_SUFFIXES = ('.exe', '.dll')


def find_signtool() -> Path:
    """SIGNTOOL (a path), else the newest x64 signtool of an installed Windows SDK, else PATH."""
    given = os.environ.get('SIGNTOOL')
    if given:
        if not Path(given).is_file():
            raise SystemExit(f'SIGNTOOL does not name a file: {given}')
        return Path(given)
    kits = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Windows Kits' / '10' / 'bin'
    found = sorted(kits.glob('10.*/x64/signtool.exe'), key=lambda p: [int(x) for x in p.parts[-3].split('.')]) if kits.is_dir() else []
    if found:
        return found[-1]
    on_path = shutil.which('signtool')
    if on_path:
        return Path(on_path)
    raise SystemExit('signtool.exe not found: install the Windows SDK signing tools or set SIGNTOOL to its path')


def has_signature(path: Path) -> bool:
    """True when the PE file carries a certificate table (structure only; see Signer.check for validity)."""
    data = path.read_bytes()[:4096]
    if len(data) < 0x40 or data[:2] != b'MZ':
        return False
    pe = int.from_bytes(data[0x3C:0x40], 'little')
    if pe + 24 + 2 > len(data) or data[pe:pe + 4] != b'PE\0\0':
        return False
    opt = pe + 24
    directories = opt + (112 if int.from_bytes(data[opt:opt + 2], 'little') == 0x20B else 96)
    if directories + 8 * 5 > len(data):
        return False
    return int.from_bytes(data[directories + 32:directories + 36], 'little') != 0 and \
        int.from_bytes(data[directories + 36:directories + 40], 'little') != 0


def trust_code(path: Path) -> int:
    """WinVerifyTrust on `path` (no UI, no network revocation fetch): 0 or an HRESULT as an unsigned 32-bit value."""
    class Guid(ctypes.Structure):
        _fields_ = [('a', ctypes.c_uint32), ('b', ctypes.c_uint16), ('c', ctypes.c_uint16), ('d', ctypes.c_ubyte * 8)]

    class FileInfo(ctypes.Structure):
        _fields_ = [('cbStruct', ctypes.c_uint32), ('pcwszFilePath', ctypes.c_wchar_p), ('hFile', ctypes.c_void_p),
                    ('pgKnownSubject', ctypes.c_void_p)]

    class Data(ctypes.Structure):
        _fields_ = [('cbStruct', ctypes.c_uint32), ('pPolicyCallbackData', ctypes.c_void_p), ('pSIPClientData', ctypes.c_void_p),
                    ('dwUIChoice', ctypes.c_uint32), ('fdwRevocationChecks', ctypes.c_uint32), ('dwUnionChoice', ctypes.c_uint32),
                    ('pFile', ctypes.POINTER(FileInfo)), ('dwStateAction', ctypes.c_uint32), ('hWVTStateData', ctypes.c_void_p),
                    ('pwszURLReference', ctypes.c_wchar_p), ('dwProvFlags', ctypes.c_uint32), ('dwUIContext', ctypes.c_uint32),
                    ('pSignatureSettings', ctypes.c_void_p)]
    action = Guid(0x00AAC56B, 0xCD44, 0x11D0, (ctypes.c_ubyte * 8)(0x8C, 0xC2, 0x00, 0xC0, 0x4F, 0xC2, 0x95, 0xEE))  # GENERIC_VERIFY_V2
    info = FileInfo(ctypes.sizeof(FileInfo), str(path), None, None)
    data = Data()
    data.cbStruct = ctypes.sizeof(Data)
    data.dwUIChoice = 2            # WTD_UI_NONE
    data.fdwRevocationChecks = 0   # WTD_REVOKE_NONE
    data.dwUnionChoice = 1         # WTD_CHOICE_FILE
    data.pFile = ctypes.pointer(info)
    data.dwStateAction = 1         # WTD_STATEACTION_VERIFY
    data.dwProvFlags = 0x1000      # WTD_CACHE_ONLY_URL_RETRIEVAL: never the network
    verify = ctypes.WinDLL('wintrust').WinVerifyTrust
    verify.restype = ctypes.c_long
    verify.argtypes = [ctypes.c_void_p, ctypes.POINTER(Guid), ctypes.POINTER(Data)]
    result = verify(None, ctypes.byref(action), ctypes.byref(data)) & 0xFFFFFFFF
    data.dwStateAction = 2         # WTD_STATEACTION_CLOSE
    verify(None, ctypes.byref(action), ctypes.byref(data))
    return result


_INFO_SCRIPT = (
    "$ErrorActionPreference='Stop';"
    "$r=@(foreach($p in $args){$s=Get-AuthenticodeSignature -LiteralPath $p;"
    "[pscustomobject]@{path=$p;status=[string]$s.Status;"
    "signer=$(if($s.SignerCertificate){$s.SignerCertificate.Subject}else{''});"
    "thumbprint=$(if($s.SignerCertificate){$s.SignerCertificate.Thumbprint}else{''});"
    "timestamper=$(if($s.TimeStamperCertificate){$s.TimeStamperCertificate.Subject}else{''})}});"
    "ConvertTo-Json -Compress -InputObject $r")


def signature_info(paths: list[Path]) -> list[dict]:
    """Signer subject, thumbprint and timestamp authority of each file (Get-AuthenticodeSignature)."""
    if not paths:
        return []
    r = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-Command',
                        '& {' + _INFO_SCRIPT + '} ' + ' '.join("'" + str(p).replace("'", "''") + "'" for p in paths)],
                       capture_output=True, text=True, encoding='utf-8', errors='replace')
    if r.returncode:
        raise SystemExit('Get-AuthenticodeSignature failed: ' + (r.stderr or r.stdout).strip()[:400])
    return json.loads(r.stdout)


class Signer:
    """Signs and checks PE files. `test` = a test certificate file: an untrusted root is then accepted, nothing else."""

    def __init__(self, name: str | None = None, pfx: Path | None = None, expect: str | None = None,
                 timestamp_url: str | None = DEFAULT_TIMESTAMP_URL):
        if bool(name) == bool(pfx):
            raise SystemExit('signing needs exactly one of --sign-name (a certificate of the user store) or --sign-pfx (a test file)')
        if pfx and not Path(pfx).is_file():
            raise SystemExit(f'test certificate file missing: {pfx}')
        if timestamp_url is None:   # offline tests only: a real signature without a timestamp dies with its certificate
            if not pfx:
                raise SystemExit('only a test certificate may sign without a timestamp')
        elif not timestamp_url.startswith(('http://', 'https://')):
            raise SystemExit('the timestamp URL must be an http(s) RFC 3161 service')
        self.name, self.pfx, self.test = name, Path(pfx) if pfx else None, bool(pfx)
        self.expect = expect or name
        if not self.expect:
            raise SystemExit('--sign-expect is required with --sign-pfx: the subject text the test certificate must show')
        self.timestamp_url = timestamp_url
        self.signtool = find_signtool()
        self.signed: list[Path] = []

    def sign(self, path: Path) -> None:
        if has_signature(path):
            raise SystemExit(f'refusing to sign a file that already has a signature: {path.name}')
        select = ['/f', str(self.pfx)] if self.pfx else ['/n', self.name]
        stamp = ['/tr', self.timestamp_url, '/td', 'SHA256'] if self.timestamp_url else []
        cmd = [str(self.signtool), 'sign', '/fd', 'SHA256', *stamp, *select, str(path)]
        last = ''
        for attempt in range(3):   # a timestamp service can fail for a moment; the file is untouched when signtool fails
            r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace')
            if r.returncode == 0:
                self.signed.append(path)
                return
            last = (r.stdout + r.stderr).strip()
            if has_signature(path):
                raise SystemExit(f'signtool failed after writing a signature on {path.name}: {last[:400]}')
            time.sleep(5 * (attempt + 1))
        raise SystemExit(f'signtool could not sign {path.name}: {last[:400]}')

    def check(self, paths: list[Path], expect: str | None = None) -> list[dict]:
        """Every file must be validly signed by `expect` (default: this signer), with a timestamp. Returns the facts."""
        want = (expect or self.expect).lower()
        facts = signature_info(paths)
        for path, fact in zip(paths, facts):
            code = trust_code(path)
            fact['trust'] = f'0x{code:08X}'
            accepted = (TRUSTED, UNTRUSTED_ROOT) if (self.test and expect is None) else (TRUSTED,)
            if code not in accepted:
                raise SystemExit(f'signature of {path.name} not accepted: WinVerifyTrust {fact["trust"]} (status {fact["status"]})')
            if want not in fact['signer'].lower():
                raise SystemExit(f'{path.name} is signed by {fact["signer"]!r}, expected {expect or self.expect!r}')
            if not fact['timestamper'] and (self.timestamp_url or expect is not None):
                raise SystemExit(f'{path.name} has no timestamp: its signature would end with the certificate')
        return facts


def is_pe_name(name: str) -> bool:
    return name.lower().endswith(PE_SUFFIXES)


if __name__ == '__main__':   # python tools/release/sign.py <file>...: read-only facts about existing signatures
    files = [Path(a) for a in sys.argv[1:]]
    for f, fact in zip(files, signature_info(files)):
        print(f.name, f'WinVerifyTrust 0x{trust_code(f):08X}', json.dumps(fact, ensure_ascii=False))
