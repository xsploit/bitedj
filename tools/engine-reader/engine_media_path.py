"""Resolve Engine media references inside an explicitly selected media root."""
from pathlib import Path, PureWindowsPath


def resolve_media(library_directory, selected_media_root, relative_path):
    root=Path(selected_media_root).resolve(strict=True)
    library=Path(library_directory).resolve(strict=True)
    if not library.is_relative_to(root):
        raise ValueError('Engine library is outside selected media root')
    if not isinstance(relative_path,str) or not relative_path or '\x00' in relative_path:
        return {'status':'invalid-reference'}
    # Database path convention is POSIX. Do not reinterpret a foreign drive,
    # UNC path or separator into an unrelated local path.
    path=Path(relative_path)
    if path.is_absolute() or PureWindowsPath(relative_path).drive or '\\' in relative_path:
        return {'status':'unsupported-reference','relativePath':relative_path}
    try:
        candidate=(library/path).resolve(strict=False)
    except (OSError,RuntimeError):
        return {'status':'invalid-reference','relativePath':relative_path}
    if not candidate.is_relative_to(root):
        return {'status':'outside-media-root','relativePath':relative_path}
    if not candidate.is_file():
        return {'status':'missing','relativePath':relative_path,'candidatePath':str(candidate)}
    return {'status':'resolved','relativePath':relative_path,'path':str(candidate),
            'sizeBytes':str(candidate.stat().st_size)}
