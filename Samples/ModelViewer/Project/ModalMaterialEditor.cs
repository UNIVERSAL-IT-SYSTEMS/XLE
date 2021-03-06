﻿// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using System;
using System.Windows.Forms;

namespace ModelViewer
{
    public partial class ModalMaterialEditor : Form
    {
        public ModalMaterialEditor()
        {
            InitializeComponent();
        }

        public string Object
        {
            set
            {
                // note --  We can get "PendingAsset" exceptions here!
                //          we need some way to handle these properly
                _matControls.Object = value;
                _preview.Object = value;
            }
        }

        public Tuple<string, ulong> PreviewModel
        {
            set
            {
                _preview.PreviewModel = value;
            }
        }
    }
}
