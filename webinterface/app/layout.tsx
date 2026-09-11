import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'water-sense ToF node',
  description:
    'Live frame viewer and configuration for the VL53L9CX people-counting node, over Web Bluetooth.',
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
