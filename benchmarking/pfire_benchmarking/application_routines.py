#!/usr/bin/env python3

import os
import subprocess as sp

import numpy as np

from configobj import ConfigObj

import flannel.io as fio

from .image_routines import load_image


class ResultObject:
    """
    Small object to hold registration result info
    """

    def __init__(self, registered_path, map_path, log_path, fixed_path=None,
                 moved_path=None):
        self.registered_path = registered_path
        self.map_path = map_path
        self.log_path = log_path
        self.fixed_path = fixed_path
        self.moved_path = moved_path


class pFIRERunnerMixin:
    """ Mixin class to provide a pFIRE runner interface
    """

    def __init__(self, *args, **kwargs):
        super(pFIRERunnerMixin, self).__init__(*args, **kwargs)
        self.pfire_fixed_path = None
        self.pfire_moved_path = None
        self.pfire_mask_path = None
        self.pfire_reg_path = None
        self.pfire_map_path = None


    def run_pfire(self, config_path, comm_size=1):
        """ Run pFIRE using provided config file
        """
        if comm_size != 1:
            raise RuntimeError("MPI pFIRE runs not yet supported")

        pfire_workdir, pfire_config = [os.path.normpath(x) for x in
                                       os.path.split(config_path)]
        config = ConfigObj(config_path)
        print("Running pFIRE on {}".format(pfire_config))

        self.pfire_fixed_path = os.path.join(pfire_workdir, config['fixed'])
        self.pfire_moved_path = os.path.join(pfire_workdir, config['moved'])
        try:
            self.pfire_mask_path = os.path.join(pfire_workdir, config['mask'])
        except KeyError:
            pass

        self.pfire_logfile = "{}_pfire.log".format(os.path.splitext(pfire_config)[0])
        with open(self.pfire_logfile, 'w') as logfile:
            pfire_args = ['pfire', pfire_config]
            print(config_path)
            res = sp.run(pfire_args, cwd=pfire_workdir, stdout=logfile,
                         stderr=logfile)

        if res.returncode != 0:
            raise RuntimeError("Failed to run pFIRE, check log for details: {}"
                               "".format(self.pfire_logfile))

        with open(self.pfire_logfile, 'r') as logfile:
            for line in logfile:
                if line.startswith("Saved registered image to "):
                    reg_path = line.replace("Saved registered image to ",
                                            "").strip()
                    self.pfire_reg_path = os.path.join(pfire_workdir, reg_path)
                elif line.startswith("Saved map to "):
                    map_path = line.replace("Saved map to ", "").strip()
                    self.pfire_map_path = os.path.join(pfire_workdir, map_path)

        if not (self.pfire_reg_path or self.pfire_map_path):
            raise RuntimeError("Failed to extract result path(s) from log")




class ShIRTRunnerMixin:
    """ Mixin class to provide a ShIRT runner interface accepted a pFIRE config
    file
    """

    default_mask_name = "default_mask.mask"
    config_defaults = {"mask": None}

    def __init__(self, *args, **kwargs):
        super(ShIRTRunnerMixin, self).__init__(*args, **kwargs)
        self.shirt_fixed_path = None
        self.shirt_moved_path = None
        self.shirt_mask_path = None
        self.shirt_reg_path = None
        self.shirt_map_path = None


    def _build_shirt_config(self, config_file):
        """
        Build ShIRT config object given pfire config file
        """
        # read pfire config and merge with default options to get full option list
        defaults = ConfigObj(self.config_defaults)
        config = ConfigObj(config_file)
        defaults.merge(config)
        return defaults

    def run_shirt(self, config_file):
        """
        Run ShIRT using a pfire config file for input
        """
        print("Running ShIRT on {}".format(config_file))
        config = self._build_shirt_config(config_file)
        print(config)

        configname = os.path.splitext(os.path.basename(config_file))[0]
        configname = configname.replace(".", "_")

        self.shirt_reg_path = 'shirt_{}_registered.image'.format(configname)
        self.shirt_map_path = 'shirt_{}_map.map'.format(configname)

        for fom in ['fixed', 'moved', 'mask']:
            if fom == 'mask' and config[fom] is None:
                continue
            if config[fom].endswith(".image"):
                setattr(self, "shirt_{}_path".format(fom), config[fom])
            else:
                data = load_image(config[fom])
                newname = os.path.splitext(config[fom])[0] + '.image'
                fio.save_image(data, newname)
                setattr(self, "shirt_{}_path".format(fom), newname)

        if config['mask'] is None:
            self.shirt_mask_path = self.default_mask_name
            data = fio.load_image(config['fixed'])
            data = np.full(data.shape, 1.0)
            fio.save_image(data, self.shirt_mask_path)

        shirt_args = ['ShIRT', 'Register', 'verbose',
                      'Fixed', self.shirt_fixed_path.replace('.image', ''),
                      'Moved', self.shirt_moved_path.replace('.image', ''),
                      'Mask', self.shirt_mask_path.replace('.mask', ''),
                      'NodeSpacing', config['nodespacing'],
                      'Registered', self.shirt_reg_path.replace('.image', ''),
                      'Map', self.shirt_map_path.replace('.map', '')]

        logfile_name = "{}_shirt.log".format(os.path.splitext(config_file)[0])
        with open(logfile_name, 'w') as logfile:
            sp.run(['ShIRT', 'setpath', 'DataPath', '.'])
            res = sp.run(shirt_args, stdout=logfile, stderr=logfile)

        if res.returncode != 0:
            raise RuntimeError("Failed to run ShIRT, check log for details: {}"
                               "".format(logfile_name))
