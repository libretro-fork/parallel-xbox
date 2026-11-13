#!/usr/bin/env python3
"""
Downloads required libraries for xemu builds on macOS from MacPorts repositories
"""
# Based on https://github.com/tpoechtrager/osxcross/blob/master/tools/osxcross-macports
# which is based on https://github.com/maci0/pmmacports
from urllib.request import urlopen
from http.client import HTTPException
import re
import os.path
import shutil
import tempfile
from tarfile import TarFile
import subprocess

# Official archive mirrors, in MacPorts' own archive_sites.tcl. Keep TLS and
# the independently pinned public key/signature checks on every mirror.
# https://github.com/macports/macports-ports/blob/master/_resources/port1.0/fetch/archive_sites.tcl
MIRRORS = ('https://packages.macports.org',
           'https://nue.de.packages.macports.org',
           'https://fra.de.packages.macports.org')
URL_TIMEOUT = 30
REQUIRED_PACKAGES = ('libepoxy',)

# FIXME: Inline macports key
# FIXME: Move packages to archive directory to track used vs unused

class LibInstaller:
	DARWIN_TARGET_X64="darwin_21" # macOS 12
	DARWIN_TARGET_ARM64="darwin_23" # macOS 14.x

	def __init__(self, arch):
		self._queue = []
		self._installed = []
		if arch == 'x86_64':
			self._darwin_target = self.DARWIN_TARGET_X64
		elif arch == 'arm64':
			self._darwin_target = self.DARWIN_TARGET_ARM64
		else:
			assert False, "Add arch"
		self._arch = arch

		self._extract_path = os.path.realpath(f'./macos-libs/{self._arch}')
		if not os.path.exists(self._extract_path):
			os.makedirs(self._extract_path)
		self._installed_path = os.path.join(self._extract_path, 'INSTALLED')
		self._pkgs_path = os.path.realpath(os.path.join(f'./macos-pkgs'))
		if not os.path.exists(self._pkgs_path):
			os.makedirs(self._pkgs_path)

	def get_latest_pkg_filename_url(self, pkg_name):
		errors = []
		for mirror in MIRRORS:
			pkg_base_url = f'{mirror}/{pkg_name}'
			try:
				with urlopen(pkg_base_url + '/', timeout=URL_TIMEOUT) as response:
					pkg_list = response.read().decode('utf-8')
				pkgs = re.findall(re.escape(pkg_name) + r'[\w\.\-\_\+]*?\.(?:any_any|darwin_any|' + self._darwin_target + r')\.(?:noarch|' + self._arch + r')\.tbz2', pkg_list)
				if pkgs:
					pkg_filename = pkgs[-1]
					print(f'    [*] Using archive mirror {mirror}')
					return pkg_filename, f'{pkg_base_url}/{pkg_filename}'
				errors.append(f'{mirror}: no compatible {self._darwin_target}.{self._arch} archive')
			except (OSError, HTTPException, UnicodeError) as error:
				errors.append(f'{mirror}: {error}')
		raise RuntimeError(f'Unable to find {pkg_name}: ' + '; '.join(errors))

	def download_urls(self, url):
		# Retrying an archive keeps its exact filename/version, never silently
		# selects a different package or changes the public-key trust root.
		yield url
		for source in MIRRORS:
			if url.startswith(source + '/'):
				suffix = url[len(source):]
				for mirror in MIRRORS:
					if mirror != source:
						yield mirror + suffix
				break

	def is_pkg_installed(self, pkg_name):
		if not os.path.exists(self._installed_path):
			return False
		with open(self._installed_path) as f:
			installed = [l.strip().split('=')[0] for l in f.readlines()]
			return pkg_name in installed

	def mark_pkg_installed(self, pkg_name, pkg_version):
		if self.is_pkg_installed(pkg_name):
			return
		with open(os.path.join(self._extract_path, 'INSTALLED'), 'a+') as f:
			f.write(f'{pkg_name}={pkg_version}\n')

	def download_file(self, desc, url, dst):
		if os.path.exists(dst) and os.path.getsize(dst):
			print(f'    [+] Already have {desc}')
			return
		print(f'    [+] Downloading {desc}')
		errors = []
		for candidate in self.download_urls(url):
			temporary = None
			try:
				with urlopen(candidate, timeout=URL_TIMEOUT) as response:
					header = response.headers.get('Content-Length')
					try:
						length = int(header) if header is not None else None
					except ValueError as error:
						raise OSError('Invalid download Content-Length') from error
					if length is not None and length < 0:
						raise OSError('Negative download Content-Length')
					with tempfile.NamedTemporaryFile(dir=os.path.dirname(dst) or '.',
							prefix=os.path.basename(dst) + '.', suffix='.tmp', delete=False) as output:
						temporary = output.name
						shutil.copyfileobj(response, output, 64 * 1024)
						# Bounded HTTPResponse.read(n) can return EOF early without
						# raising IncompleteRead. Never cache that truncated body.
						if length is not None and output.tell() != length:
							raise OSError('Incomplete download body')
				os.replace(temporary, dst)
				return
			except (OSError, HTTPException) as error:
				errors.append(f'{candidate}: {error}')
			finally:
				if temporary and os.path.exists(temporary):
					os.unlink(temporary)
		raise RuntimeError(f'Unable to download {desc}: ' + '; '.join(errors))

	def verify_pkg(self, pkg_path, sig_path):
		PUBKEYURL="https://raw.githubusercontent.com/macports/macports-base/master/macports-pubkey.pem"
		PUBKEYRMD160="d3a22f5be7184d6575afcc1be6fdb82fd25562e8"
		PUBKEYSHA1="214baa965af76ff71187e6c1ac91c559547f48ab"
		key_filename = 'macports-pubkey.pem'
		dst_key_filename = os.path.join(self._pkgs_path, key_filename)
		self.download_file('MacPorts key', PUBKEYURL, dst_key_filename)
		rmd160 = subprocess.run('openssl rmd160 "' + dst_key_filename + "\" | awk '{print $2}'",
	                            capture_output=True, shell=True,
	                            check=True).stdout.decode('utf-8').strip()
		sha1 = subprocess.run('openssl sha1 "' + dst_key_filename + "\" | awk '{print $2}'",
	                            capture_output=True, shell=True,
	                            check=True).stdout.decode('utf-8').strip()
		assert (rmd160 == PUBKEYRMD160 and sha1 == PUBKEYSHA1), 'Invalid MacPorts key'
		sha1 = subprocess.run('openssl dgst -ripemd160 '
			                  f'-verify "{dst_key_filename}" '
			                  f'-signature "{sig_path}" "{pkg_path}"',
	                            shell=True, check=True)

	def is_pkg_skipped(self, pkg_name):
		return any(pkg_name.startswith(n) for n in ('python', 'ncurses', 'mesa', 'llvm', 'libsndfile'))

	def install_pkg(self, pkg_name):
		if self.is_pkg_installed(pkg_name):
			print(f'[*] Package {pkg_name} already installed')
			return

		if self.is_pkg_skipped(pkg_name):
			print(f'[*] Skipping package {pkg_name}')
			return

		print(f'[*] Fetching {pkg_name}')
		pkg_filename, pkg_url = self.get_latest_pkg_filename_url(pkg_name)
		pkg_version = pkg_filename[re.search(r'-\d', pkg_filename).span()[0]+1:]
		pkg_version = pkg_version[:pkg_version.find('.'+self._darwin_target)]
		dst_pkg_filename = os.path.join(self._pkgs_path, pkg_filename)
		print(f'    [*] Found package {pkg_filename}')
		self.download_file(pkg_filename, pkg_url, dst_pkg_filename)

		dst_pkg_sig_filename = dst_pkg_filename + '.rmd160'
		pkg_sig_url = pkg_url + '.rmd160'
		self.download_file('package signature', pkg_sig_url, dst_pkg_sig_filename)

		print(f'    [+] Verifying package')
		self.verify_pkg(dst_pkg_filename, dst_pkg_sig_filename)

		print(f'    [+] Looking for dependencies')
		tb = TarFile.open(dst_pkg_filename)
		pkg_contents_file = tb.extractfile('./+CONTENTS').read().decode('utf-8')
		for dep in re.findall(r'@pkgdep (.+)', pkg_contents_file):
			print(f'        [>] {dep}')
			s = re.search(r'-\d', dep)
			if s:
				dep = dep[0:s.span()[0]]
			self._queue.append(dep)

		print(f'    [*] Checking tarball...')

		for fpath in tb.getnames():
			extracted_path = os.path.realpath(os.path.join(self._extract_path, fpath))
			assert extracted_path.startswith(self._extract_path), f'tarball has a global file: {fname}'

		print(f'    [*] Extracting to {self._extract_path}')
		tb.extractall(self._extract_path, numeric_owner=True)

		for fpath in tb.getnames():
			# FIXME: Symlinks
			extracted_path = os.path.realpath(os.path.join(self._extract_path, fpath))
			if extracted_path.endswith('.pc'):
				print(f'    [*] Fixing {extracted_path}')
				with open(extracted_path, 'r') as f:
					lines = f.readlines()
				for i, l in enumerate(lines):
					if l.strip().startswith('prefix'):
						new_prefix = f'prefix={self._extract_path}/opt/local\n'
						if pkg_name.startswith('openssl'): # FIXME
							new_prefix = f'prefix={self._extract_path}/opt/local/libexec/openssl11\n'
						lines[i] = new_prefix
					elif l.strip().startswith('Requires.private:'):
						if pkg_name.startswith('libepoxy'):
							lines[i] = ''
				with open(extracted_path, 'w') as f:
					f.write(''.join(lines))

		if pkg_name == 'glib2':
			fpath = './opt/local/include/glib-2.0/glib/gi18n.h'
			extracted_path = os.path.realpath(os.path.join(self._extract_path, fpath))
			print(f'    [*] Fixing {extracted_path}')
			with open(extracted_path, 'r') as f:
				lines = f.read()
			s = '/opt/local/include/libintl.h'
			lines = lines.replace(s, self._extract_path + s)
			with open(extracted_path, 'w') as f:
				f.write(lines)

		self.mark_pkg_installed(pkg_name, pkg_version)

	def install_pkgs(self, requested):
		self._queue.extend(requested)
		while len(self._queue) > 0:
			pkg_name = self._queue.pop(0)
			self.install_pkg(pkg_name)

def main():
	import argparse
	ap = argparse.ArgumentParser()
	ap.add_argument('arch', choices=('arm64', 'x86_64'))
	args = ap.parse_args()
	li = LibInstaller(args.arch)
	# The static core vendors GLib/libslirp and has no SDL, host audio or USB
	# dependency. Keep only epoxy and the signed dependencies it declares.
	li.install_pkgs(REQUIRED_PACKAGES)


if __name__ == '__main__':
	main()