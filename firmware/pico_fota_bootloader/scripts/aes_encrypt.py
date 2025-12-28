#!/usr/bin/env python3
# Copyright (c) 2024 Jakub Zimnol
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

from argparse import ArgumentParser
from Crypto.Cipher import AES
import os


def encrypt(key, data):
    cipher = AES.new(key.encode('utf-8'), AES.MODE_ECB)
    return cipher.encrypt(data)


def _main():
    parser = ArgumentParser(description='Encrypt a binary file with AES ECB algorithm.')
    parser.add_argument('-t', '--target-file', help='Path to the firmware file', required=True)
    parser.add_argument('-k', '--aes-key', help='AES key used for encryption', required=True)

    args = parser.parse_args()

    aes_key = args.aes_key
    binary_file_path = args.target_file

    if (len(aes_key) != 32):
        raise ValueError("AES: key must be 32 characters long")
    if not os.path.exists(binary_file_path):
        raise FileNotFoundError(f"AES: file {binary_file_path} does not exist")
    if not binary_file_path.endswith('.bin'):
        raise ValueError(f"AES: file {binary_file_path} is not a binary file")

    file_path_no_ext = binary_file_path.rsplit('.', 1)[0]
    output_file_path = file_path_no_ext + "_encrypted" + '.bin'

    print(f"AES: using binary: {binary_file_path}")
    print(f"AES: using key: {aes_key}")

    try:
        os.remove(output_file_path)
    except FileNotFoundError:
        pass

    with open(binary_file_path, 'rb') as file:
        binary_file_data = file.read()

    encrypted_binary_data = encrypt(aes_key, binary_file_data)
    with open(output_file_path, 'wb') as file:
        file.write(encrypted_binary_data)

    print(f"AES: output path: {output_file_path}")


if __name__ == '__main__':
    _main()
