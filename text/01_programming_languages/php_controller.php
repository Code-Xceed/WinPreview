<?php
declare(strict_types=1);

namespace WinPreview\Http;

final class DocumentController
{
    private array $supportedFormats = [
        'text' => ['txt', 'md', 'py', 'cpp', 'ts', 'json'],
        'spreadsheet' => ['csv', 'tsv', 'tab'],
        'office' => ['docx', 'pptx', 'xlsx']
    ];

    public function getCapabilities(string $extension): array
    {
        $ext = strtolower(ltrim($extension, '.'));
        foreach ($this->supportedFormats as $category => $extensions) {
            if (in_array($ext, $extensions, true)) {
                return [
                    'status' => 'supported',
                    'category' => $category,
                    'native_editing' => ($category === 'text' || $category === 'spreadsheet'),
                ];
            }
        }

        return ['status' => 'unsupported'];
    }
}
