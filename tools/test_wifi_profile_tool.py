import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import wifi_profiles as tool


class ProfileToolTests(unittest.TestCase):
    def test_upsert_preserves_order_and_updates_only_matching_ssid(self):
        p=[('home','password1'),('office','password2')]
        self.assertEqual(tool.upsert(p,'home','updated1'),[('home','updated1'),p[1]])
        self.assertEqual(tool.upsert(p,'phone','password3'),p+[('phone','password3')])
        self.assertEqual(p,[('home','password1'),('office','password2')])

    def test_capacity_rejection_keeps_list_and_existing_update_still_works(self):
        p=[(str(i),'password') for i in range(10)]
        with self.assertRaises(ValueError):tool.upsert(p,'extra','password')
        self.assertEqual(len(tool.upsert(p,'4','new-password')),10)
        self.assertEqual(len(p),10)

    def test_interrupted_publish_recovers_previous_list(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'wifi.json';original=[('home','password')]
            tool.save(p,original)
            replace=os.replace
            def fail_publish(source,target):
                if str(source).endswith('.tmp'):raise OSError('injected')
                return replace(source,target)
            with patch.object(tool.os,'replace',side_effect=fail_publish):
                with self.assertRaises(OSError):tool.save(p,[('office','password')])
            self.assertEqual(tool.read(p),original)
            tool.save(p,[('office','password')])
            self.assertEqual(tool.read(p),[('office','password')])
            self.assertFalse(Path(str(p)+'.bak').exists())

    def test_broken_main_does_not_use_backup_or_overwrite_file(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'wifi.json';p.write_text('broken')
            tool.save(Path(str(p)+'.bak'),[('home','password')])
            with self.assertRaises(ValueError):tool.read(p)
            self.assertEqual(p.read_text(),'broken')

    def test_cli_upsert_move_remove_and_empty_list(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'wifi.json';config=Path(d)/'private.env'
            script=Path(tool.__file__)
            def run(action,*args):
                output=subprocess.check_output([sys.executable,str(script),action,'--file',str(p),*args],text=True)
                self.assertNotIn('network-private',output)
                self.assertNotIn('password-private',output)
                return json.loads(output)
            for name in ('network-private-one','network-private-two'):
                config.write_text('WIFI_SSID='+name+'\nWIFI_PASSWORD=password-private\n')
                run('upsert','--config',str(config))
            run('move','--index','2','--to','1')
            self.assertEqual(tool.read(p)[0][0],'network-private-two')
            run('remove','--index','1');run('remove','--index','1')
            self.assertEqual(tool.read(p),[])
            self.assertEqual(run('status')['networks'],0)
