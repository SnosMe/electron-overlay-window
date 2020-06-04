{
  'targets': [
    {
      'target_name': 'overlay_window',
      'sources': [
        'src/lib/addon.c',
        'src/lib/napi_helpers.c'
      ],
      'include_dirs': [
        'src/lib'
      ],
      'conditions': [
        ['OS=="win"', {
          'defines': [
            'WIN32_LEAN_AND_MEAN'
          ],
      	  'sources': [
            'src/lib/windows.c',
          ]
      	}],
        ['OS=="linux"', {
      	  'sources': [
            'src/lib/x11.c',
          ]
      	}]
      ]
    }
  ]
}